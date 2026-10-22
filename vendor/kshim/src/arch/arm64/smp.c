#include <smp.h>

#include <config.h>
#include <kshim_mmu.h>
#include <libfdt.h>
#include <psci.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef CONFIG_KSHIM_SMP_WORKERS
#define CONFIG_KSHIM_SMP_WORKERS 3
#endif

#define KSHIM_SMP_STACK_SIZE UINT64_C(0x10000)
#define KSHIM_MPIDR_AFFINITY_MASK UINT64_C(0x000000ff00ffffff)
#define KSHIM_US_PER_SECOND UINT64_C(1000000)

enum kshim_smp_cpu_state {
    KSHIM_CPU_OFF = 0,
    KSHIM_CPU_BOOTING,
    KSHIM_CPU_ONLINE,
    KSHIM_CPU_STOPPING,
    KSHIM_CPU_ERROR,
};

/* The assembly entry reads stack_top before enabling translation. */
typedef struct __attribute__((aligned(64))) {
    uintptr_t stack_top;
    uint64_t mpidr;
    uint32_t cpu_index;
    uint32_t reserved;
} kshim_smp_boot_context_t;

typedef struct __attribute__((aligned(64))) {
    uint64_t mpidr;
    uint64_t next_ticket;
    uint64_t submitted_ticket;
    uint64_t completed_ticket;
    kshim_smp_callback_t task_callback;
    void *task_context;
    int task_result;
    uint32_t state;
    uint32_t shutdown;
    uint32_t started;
    uint32_t permanent_active;
    kshim_smp_callback_t permanent_callback;
    void *permanent_context;
} kshim_smp_cpu_t;

typedef struct {
    kshim_smp_cpu_t cpus[KSHIM_SMP_MAX_CPUS];
    kshim_smp_boot_context_t boot[KSHIM_SMP_MAX_CPUS];
    size_t cpu_count;
    size_t worker_limit;
    uint32_t initialized;
} kshim_smp_context_t;

typedef struct {
    uint64_t start;
    uint64_t duration;
} kshim_smp_deadline_t;

static uint8_t mSmpStacks[KSHIM_SMP_MAX_CPUS][KSHIM_SMP_STACK_SIZE]
    __attribute__((aligned(64), section(".smp_stacks")));
static kshim_smp_context_t mSmp;

static uint64_t read_mpidr(void)
{
#if defined(__aarch64__)
    uint64_t value;

    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(value));
    return value & KSHIM_MPIDR_AFFINITY_MASK;
#else
    return 0U;
#endif
}

uint64_t kshim_smp_time_ticks(void)
{
#if defined(__aarch64__)
    uint64_t value;

    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(value));
    return value;
#else
    return 0U;
#endif
}

uint64_t kshim_smp_time_frequency(void)
{
#if defined(__aarch64__)
    uint64_t value;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
#else
    return KSHIM_US_PER_SECOND;
#endif
}

uint64_t kshim_smp_time_us(void)
{
    uint64_t frequency = kshim_smp_time_frequency();
    uint64_t ticks = kshim_smp_time_ticks();
    uint64_t seconds;
    uint64_t remainder;

    if (frequency == 0U)
        return 0U;
    seconds = ticks / frequency;
    remainder = ticks % frequency;
    if (seconds > UINT64_MAX / KSHIM_US_PER_SECOND)
        return UINT64_MAX;
    return seconds * KSHIM_US_PER_SECOND +
        remainder * KSHIM_US_PER_SECOND / frequency;
}

static uint64_t timeout_ticks(uint64_t timeout_us)
{
    uint64_t frequency = kshim_smp_time_frequency();
    uint64_t seconds = timeout_us / KSHIM_US_PER_SECOND;
    uint64_t remainder = timeout_us % KSHIM_US_PER_SECOND;
    uint64_t ticks;
    uint64_t fraction;

    if (frequency == 0U)
        return 0U;
    if (seconds > UINT64_MAX / frequency)
        return UINT64_MAX;
    ticks = seconds * frequency;
    if (remainder != 0U && frequency >
            (UINT64_MAX - (KSHIM_US_PER_SECOND - 1U)) / remainder)
        return UINT64_MAX;
    fraction = (remainder * frequency + KSHIM_US_PER_SECOND - 1U) /
        KSHIM_US_PER_SECOND;
    if (ticks > UINT64_MAX - fraction)
        return UINT64_MAX;
    ticks += fraction;
    if (timeout_us != 0U && ticks == 0U)
        ticks = 1U;
    return ticks;
}

static kshim_smp_deadline_t make_deadline(uint64_t timeout_us)
{
    kshim_smp_deadline_t deadline = {
        .start = kshim_smp_time_ticks(),
        .duration = timeout_ticks(timeout_us),
    };

    return deadline;
}

static int deadline_expired(const kshim_smp_deadline_t *deadline)
{
    if (deadline->duration == UINT64_MAX)
        return 0;
    return kshim_smp_time_ticks() - deadline->start >= deadline->duration;
}

static void cpu_relax(void)
{
#if defined(__aarch64__)
    __asm__ volatile("yield" ::: "memory");
#endif
}

static void signal_workers(void)
{
#if defined(__aarch64__)
    __asm__ volatile("dsb ishst\nsev" ::: "memory");
#endif
}

static void cache_clean_range(const void *base, size_t size)
{
#if defined(__aarch64__)
    uintptr_t address = (uintptr_t)base;
    uintptr_t end;
    uint64_t ctr;
    uintptr_t line_size;

    if (size == 0U || address > UINTPTR_MAX - size)
        return;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    line_size = (uintptr_t)4U << ((ctr >> 16) & UINT64_C(0xf));
    end = address + size;
    address &= ~(line_size - 1U);
    while (address < end) {
        __asm__ volatile("dc cvac, %0" : : "r"(address) : "memory");
        address += line_size;
    }
    __asm__ volatile("dsb sy" ::: "memory");
#else
    (void)base;
    (void)size;
#endif
}

static int property_equals(
    const void *fdt, int node, const char *name, const char *expected)
{
    int length;
    const char *value = fdt_getprop(fdt, node, name, &length);
    size_t expected_length = strlen(expected) + 1U;

    return value != NULL && length == (int)expected_length &&
        memcmp(value, expected, expected_length) == 0;
}

static int cpu_node_available(const void *fdt, int node)
{
    int length;
    const char *status = fdt_getprop(fdt, node, "status", &length);

    if (status == NULL)
        return length == -FDT_ERR_NOTFOUND;
    return (length == 5 && memcmp(status, "okay", 5U) == 0) ||
        (length == 3 && memcmp(status, "ok", 3U) == 0);
}

static int read_cpu_mpidr(
    const void *fdt, int node, int address_cells, uint64_t *mpidr)
{
    int length;
    const fdt32_t *reg = fdt_getprop(fdt, node, "reg", &length);
    uint64_t value;

    if (reg == NULL || mpidr == NULL ||
        (address_cells != 1 && address_cells != 2) ||
        length < address_cells * (int)sizeof(*reg))
        return -1;
    value = fdt32_to_cpu(reg[0]);
    if (address_cells == 2)
        value = value << 32 | fdt32_to_cpu(reg[1]);
    *mpidr = value & KSHIM_MPIDR_AFFINITY_MASK;
    return 0;
}

static int mpidr_is_duplicate(uint64_t mpidr, size_t count)
{
    for (size_t index = 1U; index < count; index++) {
        if (mSmp.cpus[index].mpidr == mpidr)
            return 1;
    }
    return 0;
}

static int enumerate_cpus(const void *fdt)
{
    uint64_t boot_mpidr = read_mpidr();
    size_t count = 1U;
    int found_boot = 0;
    int parent;
    int address_cells;
    int node;

    parent = fdt_path_offset(fdt, "/cpus");
    if (parent < 0)
        return KSHIM_SMP_CPU_ERROR;
    address_cells = fdt_address_cells(fdt, parent);
    if (address_cells != 1 && address_cells != 2)
        return KSHIM_SMP_CPU_ERROR;

    mSmp.cpus[0].mpidr = boot_mpidr;
    fdt_for_each_subnode(node, fdt, parent) {
        uint64_t mpidr;

        if (!property_equals(fdt, node, "device_type", "cpu") ||
            !cpu_node_available(fdt, node) ||
            read_cpu_mpidr(fdt, node, address_cells, &mpidr) != 0)
            continue;
        if (mpidr == boot_mpidr) {
            found_boot = 1;
            continue;
        }
        if (count < KSHIM_SMP_MAX_CPUS && !mpidr_is_duplicate(mpidr, count))
            mSmp.cpus[count++].mpidr = mpidr;
    }
    if (!found_boot)
        return KSHIM_SMP_CPU_ERROR;
    mSmp.cpu_count = count;
    return KSHIM_SMP_OK;
}

int kshim_smp_init(const void *fdt)
{
    int configured_mmu = 0;
    int status;

    if (fdt == NULL || fdt_check_header(fdt) != 0)
        return KSHIM_SMP_INVALID;
    if (__atomic_load_n(&mSmp.initialized, __ATOMIC_ACQUIRE) != 0U &&
        kshim_smp_available_count() != 0U)
        return KSHIM_SMP_BUSY;

    memset(&mSmp, 0, sizeof(mSmp));
    memset(mSmpStacks, 0, sizeof(mSmpStacks));
    status = enumerate_cpus(fdt);
    if (status != KSHIM_SMP_OK)
        return status;

    status = kshim_psci_init(fdt);
    if (status != KSHIM_PSCI_SUCCESS)
        return KSHIM_SMP_PSCI_ERROR;
    if (kshim_mmu_root_table() == 0U) {
        if (kshim_mmu_configure_default(fdt) != 0)
            return KSHIM_SMP_CPU_ERROR;
        configured_mmu = 1;
    }
    if ((configured_mmu || !kshim_mmu_is_enabled()) &&
        kshim_mmu_enable_current_cpu() != 0)
        return KSHIM_SMP_CPU_ERROR;
    mSmp.worker_limit = mSmp.cpu_count - 1U;
    if (mSmp.worker_limit > (size_t)CONFIG_KSHIM_SMP_WORKERS)
        mSmp.worker_limit = (size_t)CONFIG_KSHIM_SMP_WORKERS;
    if (mSmp.worker_limit > KSHIM_SMP_MAX_CPUS - 1U)
        mSmp.worker_limit = KSHIM_SMP_MAX_CPUS - 1U;

    mSmp.cpus[0].state = KSHIM_CPU_ONLINE;
    for (size_t index = 0U; index < mSmp.cpu_count; index++) {
        mSmp.boot[index].stack_top =
            (uintptr_t)&mSmpStacks[index][KSHIM_SMP_STACK_SIZE];
        mSmp.boot[index].mpidr = mSmp.cpus[index].mpidr;
        mSmp.boot[index].cpu_index = (uint32_t)index;
    }
    __atomic_store_n(&mSmp.initialized, 1U, __ATOMIC_RELEASE);
    return KSHIM_SMP_OK;
}

static int start_worker(size_t cpu_index, const kshim_smp_deadline_t *deadline)
{
    kshim_smp_cpu_t *cpu = &mSmp.cpus[cpu_index];
    kshim_smp_boot_context_t *boot = &mSmp.boot[cpu_index];
    int64_t psci_status;

    cpu->next_ticket = 0U;
    cpu->submitted_ticket = 0U;
    cpu->completed_ticket = 0U;
    cpu->task_callback = NULL;
    cpu->task_context = NULL;
    cpu->task_result = 0;
    cpu->shutdown = 0U;
    cpu->started = 0U;
    cpu->permanent_active = 0U;
    cpu->permanent_callback = NULL;
    cpu->permanent_context = NULL;
    __atomic_store_n(&cpu->state, KSHIM_CPU_BOOTING, __ATOMIC_RELEASE);

    cache_clean_range(boot, sizeof(*boot));
    cache_clean_range(cpu, sizeof(*cpu));
    psci_status = kshim_psci_cpu_on(
        cpu->mpidr, (uintptr_t)kshim_smp_secondary_entry, (uintptr_t)boot);
    if (psci_status != KSHIM_PSCI_SUCCESS) {
        __atomic_store_n(&cpu->state, KSHIM_CPU_ERROR, __ATOMIC_RELEASE);
        return KSHIM_SMP_PSCI_ERROR;
    }
    __atomic_store_n(&cpu->started, 1U, __ATOMIC_RELEASE);

    for (;;) {
        uint32_t state = __atomic_load_n(&cpu->state, __ATOMIC_ACQUIRE);

        if (state == KSHIM_CPU_ONLINE)
            return KSHIM_SMP_OK;
        if (state == KSHIM_CPU_ERROR)
            return KSHIM_SMP_CPU_ERROR;
        if (deadline_expired(deadline))
            return KSHIM_SMP_TIMEOUT;
        cpu_relax();
    }
}

int kshim_smp_start_workers(uint64_t timeout_us)
{
    kshim_smp_deadline_t deadline;

    if (__atomic_load_n(&mSmp.initialized, __ATOMIC_ACQUIRE) == 0U)
        return KSHIM_SMP_NOT_READY;
    deadline = make_deadline(timeout_us);
    for (size_t worker = 0U; worker < mSmp.worker_limit; worker++) {
        size_t cpu_index = worker + 1U;
        uint32_t state =
            __atomic_load_n(&mSmp.cpus[cpu_index].state, __ATOMIC_ACQUIRE);
        int status;

        if (state == KSHIM_CPU_ONLINE)
            continue;
        if (state != KSHIM_CPU_OFF && state != KSHIM_CPU_ERROR)
            return KSHIM_SMP_BUSY;
        status = start_worker(cpu_index, &deadline);
        if (status != KSHIM_SMP_OK)
            return status;
    }
    return KSHIM_SMP_OK;
}

size_t kshim_smp_cpu_count(void)
{
    if (__atomic_load_n(&mSmp.initialized, __ATOMIC_ACQUIRE) == 0U)
        return 0U;
    return mSmp.cpu_count;
}

size_t kshim_smp_online_count(void)
{
    size_t count = 0U;

    if (__atomic_load_n(&mSmp.initialized, __ATOMIC_ACQUIRE) == 0U)
        return 0U;
    for (size_t index = 0U; index < mSmp.cpu_count; index++) {
        if (__atomic_load_n(&mSmp.cpus[index].state, __ATOMIC_ACQUIRE) ==
            KSHIM_CPU_ONLINE)
            count++;
    }
    return count;
}

size_t kshim_smp_available_count(void)
{
    size_t count = 0U;

    if (__atomic_load_n(&mSmp.initialized, __ATOMIC_ACQUIRE) == 0U)
        return 0U;
    for (size_t index = 1U; index <= mSmp.worker_limit; index++) {
        if (__atomic_load_n(&mSmp.cpus[index].state, __ATOMIC_ACQUIRE) ==
            KSHIM_CPU_ONLINE)
            count++;
    }
    return count;
}

uint64_t kshim_smp_cpu_mpidr(size_t cpu_index)
{
    if (__atomic_load_n(&mSmp.initialized, __ATOMIC_ACQUIRE) == 0U ||
        cpu_index >= mSmp.cpu_count)
        return UINT64_MAX;
    return mSmp.cpus[cpu_index].mpidr;
}

static kshim_smp_cpu_t *worker_cpu(size_t worker_index)
{
    if (__atomic_load_n(&mSmp.initialized, __ATOMIC_ACQUIRE) == 0U ||
        worker_index >= mSmp.worker_limit)
        return NULL;
    return &mSmp.cpus[worker_index + 1U];
}

int kshim_smp_submit(
    size_t worker_index, kshim_smp_callback_t callback, void *context,
    kshim_smp_ticket_t *ticket)
{
    kshim_smp_cpu_t *cpu = worker_cpu(worker_index);
    uint64_t submitted;
    uint64_t completed;
    uint64_t next;

    if (cpu == NULL || callback == NULL || ticket == NULL)
        return KSHIM_SMP_INVALID;
    if (__atomic_load_n(&cpu->state, __ATOMIC_ACQUIRE) != KSHIM_CPU_ONLINE ||
        __atomic_load_n(&cpu->shutdown, __ATOMIC_ACQUIRE) != 0U)
        return KSHIM_SMP_NOT_READY;
    submitted = __atomic_load_n(&cpu->submitted_ticket, __ATOMIC_ACQUIRE);
    completed = __atomic_load_n(&cpu->completed_ticket, __ATOMIC_ACQUIRE);
    if (submitted != completed)
        return KSHIM_SMP_BUSY;

    next = __atomic_add_fetch(&cpu->next_ticket, 1U, __ATOMIC_RELAXED);
    if (next == 0U)
        next = __atomic_add_fetch(&cpu->next_ticket, 1U, __ATOMIC_RELAXED);
    cpu->task_callback = callback;
    cpu->task_context = context;
    cpu->task_result = 0;
    __atomic_store_n(&cpu->submitted_ticket, next, __ATOMIC_RELEASE);
    *ticket = next;
    signal_workers();
    return KSHIM_SMP_OK;
}

int kshim_smp_join(
    size_t worker_index, kshim_smp_ticket_t ticket, uint64_t timeout_us,
    int *result)
{
    kshim_smp_cpu_t *cpu = worker_cpu(worker_index);
    kshim_smp_deadline_t deadline;

    if (cpu == NULL || ticket == 0U)
        return KSHIM_SMP_INVALID;
    if (__atomic_load_n(&cpu->submitted_ticket, __ATOMIC_ACQUIRE) != ticket)
        return KSHIM_SMP_INVALID;
    deadline = make_deadline(timeout_us);
    while (__atomic_load_n(&cpu->completed_ticket, __ATOMIC_ACQUIRE) != ticket) {
        if (__atomic_load_n(&cpu->state, __ATOMIC_ACQUIRE) == KSHIM_CPU_ERROR)
            return KSHIM_SMP_CPU_ERROR;
        if (deadline_expired(&deadline))
            return KSHIM_SMP_TIMEOUT;
        cpu_relax();
    }
    if (result != NULL)
        *result = cpu->task_result;
    return KSHIM_SMP_OK;
}

int kshim_smp_set_permanent_callback(
    size_t worker_index, kshim_smp_callback_t callback, void *context)
{
    kshim_smp_cpu_t *cpu = worker_cpu(worker_index);

    if (cpu == NULL)
        return KSHIM_SMP_INVALID;
    if (__atomic_load_n(&cpu->state, __ATOMIC_ACQUIRE) != KSHIM_CPU_ONLINE ||
        __atomic_load_n(&cpu->shutdown, __ATOMIC_ACQUIRE) != 0U)
        return KSHIM_SMP_NOT_READY;

    if (callback != NULL) {
        if (__atomic_load_n(&cpu->permanent_callback, __ATOMIC_ACQUIRE) != NULL ||
            __atomic_load_n(&cpu->permanent_active, __ATOMIC_ACQUIRE) != 0U)
            return KSHIM_SMP_BUSY;
        cpu->permanent_context = context;
        __atomic_store_n(
            &cpu->permanent_callback, callback, __ATOMIC_RELEASE);
        signal_workers();
        return KSHIM_SMP_OK;
    }

    __atomic_store_n(&cpu->permanent_callback, NULL, __ATOMIC_RELEASE);
    signal_workers();
    for (;;) {
        uint32_t expected = 0U;

        if (__atomic_compare_exchange_n(
                &cpu->permanent_active, &expected, 1U, 0,
                __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            break;
        cpu_relax();
    }
    cpu->permanent_context = NULL;
    __atomic_store_n(&cpu->permanent_active, 0U, __ATOMIC_RELEASE);
    return KSHIM_SMP_OK;
}

int kshim_smp_shutdown(uint64_t timeout_us)
{
    kshim_smp_deadline_t deadline;

    if (__atomic_load_n(&mSmp.initialized, __ATOMIC_ACQUIRE) == 0U)
        return KSHIM_SMP_NOT_READY;

    for (size_t index = 1U; index <= mSmp.worker_limit; index++) {
        if (__atomic_load_n(&mSmp.cpus[index].started, __ATOMIC_ACQUIRE) != 0U)
            __atomic_store_n(&mSmp.cpus[index].shutdown, 1U, __ATOMIC_RELEASE);
    }
    signal_workers();
    deadline = make_deadline(timeout_us);

    for (size_t index = 1U; index <= mSmp.worker_limit; index++) {
        kshim_smp_cpu_t *cpu = &mSmp.cpus[index];

        if (__atomic_load_n(&cpu->started, __ATOMIC_ACQUIRE) == 0U)
            continue;
        for (;;) {
            int64_t affinity =
                kshim_psci_affinity_info(cpu->mpidr, 0U);

            if (affinity == KSHIM_PSCI_AFFINITY_OFF) {
                __atomic_store_n(&cpu->state, KSHIM_CPU_OFF, __ATOMIC_RELEASE);
                __atomic_store_n(&cpu->started, 0U, __ATOMIC_RELEASE);
                break;
            }
            if (affinity < 0)
                return KSHIM_SMP_PSCI_ERROR;
            if (deadline_expired(&deadline))
                return KSHIM_SMP_TIMEOUT;
            cpu_relax();
        }
    }
    return KSHIM_SMP_OK;
}

int kshim_smp_prepare_handoff(uint64_t timeout_us)
{
    int status = kshim_smp_shutdown(timeout_us);

    if (status != KSHIM_SMP_OK)
        return status;
    return kshim_mmu_prepare_handoff() == 0 ?
        KSHIM_SMP_OK : KSHIM_SMP_CPU_ERROR;
}

void kshim_smp_secondary_main(uintptr_t boot_context, uint64_t entry_el)
{
    kshim_smp_boot_context_t *boot =
        (kshim_smp_boot_context_t *)boot_context;
    kshim_smp_cpu_t *cpu;

    if (boot == NULL || boot->cpu_index == 0U ||
        boot->cpu_index >= KSHIM_SMP_MAX_CPUS ||
        boot->mpidr != read_mpidr())
        goto CpuOff;
    cpu = &mSmp.cpus[boot->cpu_index];
    if (kshim_mmu_enable_secondary(entry_el) != 0) {
        __atomic_store_n(&cpu->state, KSHIM_CPU_ERROR, __ATOMIC_RELEASE);
        signal_workers();
        goto CpuOff;
    }

    __atomic_store_n(&cpu->state, KSHIM_CPU_ONLINE, __ATOMIC_RELEASE);
    signal_workers();
    for (;;) {
        uint64_t submitted;
        uint64_t completed;
        kshim_smp_callback_t callback;

        if (__atomic_load_n(&cpu->shutdown, __ATOMIC_ACQUIRE) != 0U)
            break;

        submitted =
            __atomic_load_n(&cpu->submitted_ticket, __ATOMIC_ACQUIRE);
        completed =
            __atomic_load_n(&cpu->completed_ticket, __ATOMIC_RELAXED);
        if (submitted != completed) {
            int result;

            callback = cpu->task_callback;
            result = callback != NULL ? callback(cpu->task_context) :
                KSHIM_SMP_INVALID;
            cpu->task_result = result;
            cpu->task_callback = NULL;
            cpu->task_context = NULL;
            __atomic_store_n(
                &cpu->completed_ticket, submitted, __ATOMIC_RELEASE);
            signal_workers();
            continue;
        }

        callback = __atomic_load_n(
            &cpu->permanent_callback, __ATOMIC_ACQUIRE);
        if (callback != NULL) {
            uint32_t expected = 0U;

            if (!__atomic_compare_exchange_n(
                    &cpu->permanent_active, &expected, 1U, 0,
                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                cpu_relax();
                continue;
            }
            if (__atomic_load_n(
                    &cpu->permanent_callback, __ATOMIC_ACQUIRE) == callback)
                (void)callback(cpu->permanent_context);
            __atomic_store_n(&cpu->permanent_active, 0U, __ATOMIC_RELEASE);
            signal_workers();
            cpu_relax();
            continue;
        }
#if defined(__aarch64__)
        __asm__ volatile("wfe" ::: "memory");
#else
        cpu_relax();
#endif
    }

    __atomic_store_n(&cpu->state, KSHIM_CPU_STOPPING, __ATOMIC_RELEASE);
    signal_workers();
    kshim_mmu_cpu_off_prepare();
CpuOff:
    if (kshim_psci_cpu_off() != KSHIM_PSCI_SUCCESS && boot != NULL &&
        boot->cpu_index < KSHIM_SMP_MAX_CPUS) {
        __atomic_store_n(
            &mSmp.cpus[boot->cpu_index].state, KSHIM_CPU_ERROR,
            __ATOMIC_RELEASE);
        signal_workers();
    }
    for (;;) {
#if defined(__aarch64__)
        __asm__ volatile("wfe" ::: "memory");
#endif
    }
}
