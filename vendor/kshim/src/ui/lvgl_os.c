#include <config.h>
#include <smp.h>

#include <lvgl.h>

#include <stddef.h>
#include <stdint.h>

#if LV_USE_OS == LV_OS_CUSTOM

#ifndef CONFIG_KSHIM_SMP_WORKERS
#define CONFIG_KSHIM_SMP_WORKERS 3
#endif
#ifndef KSHIM_LVGL_TEST_FORCE_DELETE_FAILURE
#define KSHIM_LVGL_TEST_FORCE_DELETE_FAILURE 0
#endif

#define KSHIM_LVGL_NO_OWNER UINT64_MAX
#define KSHIM_LVGL_NO_WORKER UINT32_MAX

static uint32_t mReservedWorkers;
static size_t mActiveThreads;
static size_t mPeakThreads;
static uint32_t mFailure;

static uint64_t current_cpu(void)
{
#if defined(__aarch64__)
    uint64_t mpidr;

    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return mpidr & UINT64_C(0x000000ff00ffffff);
#else
    return 0U;
#endif
}

static void wait_for_event(void)
{
#if defined(__aarch64__)
    __asm__ volatile("wfe" ::: "memory");
#endif
}

static void send_event(void)
{
#if defined(__aarch64__)
    __asm__ volatile("dsb ishst\nsev" ::: "memory");
#endif
}

static int reserve_worker(uint32_t worker)
{
    uint32_t bit;
    uint32_t current;

    if (worker >= 32U)
        return 0;
    bit = UINT32_C(1) << worker;
    current = __atomic_load_n(&mReservedWorkers, __ATOMIC_ACQUIRE);
    for (;;) {
        uint32_t desired;

        if ((current & bit) != 0U)
            return 0;
        desired = current | bit;
        if (__atomic_compare_exchange_n(
                &mReservedWorkers, &current, desired, 0,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return 1;
    }
}

static void release_worker(uint32_t worker)
{
    if (worker < 32U)
        (void)__atomic_fetch_and(
            &mReservedWorkers, ~(UINT32_C(1) << worker), __ATOMIC_RELEASE);
}

static void update_peak(size_t active)
{
    size_t peak = __atomic_load_n(&mPeakThreads, __ATOMIC_RELAXED);

    while (peak < active && !__atomic_compare_exchange_n(
            &mPeakThreads, &peak, active, 0,
            __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static int thread_entry(void *opaque)
{
    lv_thread_t *thread = opaque;

    if (thread == NULL || thread->callback == NULL)
        return KSHIM_SMP_INVALID;
    thread->callback(thread->user_data);
    __atomic_store_n(&thread->finished, 1U, __ATOMIC_RELEASE);
    send_event();
    return KSHIM_SMP_OK;
}

static void fail_closed(void) __attribute__((noreturn));

static void fail_closed(void)
{
    __atomic_store_n(&mFailure, 1U, __ATOMIC_RELEASE);
    for (;;) {
        wait_for_event();
    }
}

lv_result_t lv_thread_init(
    lv_thread_t *thread, lv_thread_prio_t priority,
    void (*callback)(void *), size_t stack_size, void *user_data)
{
    size_t workers;

    LV_UNUSED(priority);
    LV_UNUSED(stack_size);
    if (thread == NULL || callback == NULL)
        return LV_RESULT_INVALID;

    *thread = (lv_thread_t){
        .callback = callback,
        .user_data = user_data,
        .worker_index = KSHIM_LVGL_NO_WORKER,
    };
    workers = kshim_smp_cpu_count();
    if (workers != 0U)
        workers--;
    if (workers > (size_t)CONFIG_KSHIM_SMP_WORKERS)
        workers = (size_t)CONFIG_KSHIM_SMP_WORKERS;
    if (workers > KSHIM_SMP_MAX_CPUS - 1U)
        workers = KSHIM_SMP_MAX_CPUS - 1U;
    for (size_t worker = 0U; worker < workers; worker++) {
        kshim_smp_ticket_t ticket;
        int status;

        if (!reserve_worker((uint32_t)worker))
            continue;
        thread->worker_index = (uint32_t)worker;
        __atomic_store_n(&thread->active, 1U, __ATOMIC_RELEASE);
        status = kshim_smp_submit(worker, thread_entry, thread, &ticket);
        if (status == KSHIM_SMP_OK) {
            size_t active;

            thread->ticket = ticket;
            active = __atomic_add_fetch(
                &mActiveThreads, 1U, __ATOMIC_ACQ_REL);
            update_peak(active);
            return LV_RESULT_OK;
        }
        __atomic_store_n(&thread->active, 0U, __ATOMIC_RELEASE);
        thread->worker_index = KSHIM_LVGL_NO_WORKER;
        release_worker((uint32_t)worker);
    }
    return LV_RESULT_INVALID;
}

lv_result_t lv_thread_delete(lv_thread_t *thread)
{
    int result;
    int status;

    if (thread == NULL)
        return LV_RESULT_INVALID;
    if (thread->inline_mode != 0U)
        return LV_RESULT_OK;
    if (__atomic_load_n(&thread->active, __ATOMIC_ACQUIRE) == 0U ||
        thread->worker_index == KSHIM_LVGL_NO_WORKER || thread->ticket == 0U)
        return LV_RESULT_INVALID;

#if KSHIM_LVGL_TEST_FORCE_DELETE_FAILURE
    fail_closed();
#endif
    status = kshim_smp_join(
        thread->worker_index, thread->ticket,
        KSHIM_SMP_DEFAULT_TIMEOUT_US, &result);
    if (status != KSHIM_SMP_OK || result != KSHIM_SMP_OK)
        fail_closed();

    release_worker(thread->worker_index);
    (void)__atomic_sub_fetch(&mActiveThreads, 1U, __ATOMIC_ACQ_REL);
    __atomic_store_n(&thread->active, 0U, __ATOMIC_RELEASE);
    thread->worker_index = KSHIM_LVGL_NO_WORKER;
    return LV_RESULT_OK;
}

lv_result_t lv_mutex_init(lv_mutex_t *mutex)
{
    if (mutex == NULL)
        return LV_RESULT_INVALID;
    *mutex = (lv_mutex_t){
        .owner = KSHIM_LVGL_NO_OWNER,
        .initialized = 1U,
    };
    return LV_RESULT_OK;
}

lv_result_t lv_mutex_lock(lv_mutex_t *mutex)
{
    uint64_t owner;

    if (mutex == NULL ||
        __atomic_load_n(&mutex->initialized, __ATOMIC_ACQUIRE) == 0U)
        return LV_RESULT_INVALID;
    owner = current_cpu();
    for (;;) {
        uint32_t expected = 0U;

        if (__atomic_load_n(&mutex->locked, __ATOMIC_ACQUIRE) != 0U &&
            __atomic_load_n(&mutex->owner, __ATOMIC_RELAXED) == owner) {
            mutex->depth++;
            return LV_RESULT_OK;
        }
        if (__atomic_compare_exchange_n(
                &mutex->locked, &expected, 1U, 0,
                __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            __atomic_store_n(&mutex->owner, owner, __ATOMIC_RELAXED);
            mutex->depth = 1U;
            return LV_RESULT_OK;
        }
        wait_for_event();
    }
}

lv_result_t lv_mutex_lock_isr(lv_mutex_t *mutex)
{
    return lv_mutex_lock(mutex);
}

lv_result_t lv_mutex_unlock(lv_mutex_t *mutex)
{
    if (mutex == NULL ||
        __atomic_load_n(&mutex->initialized, __ATOMIC_ACQUIRE) == 0U ||
        __atomic_load_n(&mutex->locked, __ATOMIC_ACQUIRE) == 0U ||
        __atomic_load_n(&mutex->owner, __ATOMIC_RELAXED) != current_cpu() ||
        mutex->depth == 0U)
        return LV_RESULT_INVALID;

    mutex->depth--;
    if (mutex->depth == 0U) {
        __atomic_store_n(
            &mutex->owner, KSHIM_LVGL_NO_OWNER, __ATOMIC_RELAXED);
        __atomic_store_n(&mutex->locked, 0U, __ATOMIC_RELEASE);
        send_event();
    }
    return LV_RESULT_OK;
}

lv_result_t lv_mutex_delete(lv_mutex_t *mutex)
{
    if (mutex == NULL ||
        __atomic_load_n(&mutex->initialized, __ATOMIC_ACQUIRE) == 0U ||
        __atomic_load_n(&mutex->locked, __ATOMIC_ACQUIRE) != 0U)
        return LV_RESULT_INVALID;
    __atomic_store_n(&mutex->initialized, 0U, __ATOMIC_RELEASE);
    return LV_RESULT_OK;
}

lv_result_t lv_thread_sync_init(lv_thread_sync_t *sync)
{
    if (sync == NULL)
        return LV_RESULT_INVALID;
    __atomic_store_n(&sync->signaled, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&sync->initialized, 1U, __ATOMIC_RELEASE);
    return LV_RESULT_OK;
}

lv_result_t lv_thread_sync_wait(lv_thread_sync_t *sync)
{
    if (sync == NULL)
        return LV_RESULT_INVALID;
    for (;;) {
        if (__atomic_exchange_n(
                &sync->signaled, 0U, __ATOMIC_ACQ_REL) != 0U)
            return LV_RESULT_OK;
        if (__atomic_load_n(&sync->initialized, __ATOMIC_ACQUIRE) == 0U)
            return LV_RESULT_INVALID;
        wait_for_event();
    }
}

lv_result_t lv_thread_sync_signal(lv_thread_sync_t *sync)
{
    if (sync == NULL ||
        __atomic_load_n(&sync->initialized, __ATOMIC_ACQUIRE) == 0U)
        return LV_RESULT_INVALID;
    __atomic_store_n(&sync->signaled, 1U, __ATOMIC_RELEASE);
    send_event();
    return LV_RESULT_OK;
}

lv_result_t lv_thread_sync_signal_isr(lv_thread_sync_t *sync)
{
    return lv_thread_sync_signal(sync);
}

lv_result_t lv_thread_sync_delete(lv_thread_sync_t *sync)
{
    if (sync == NULL ||
        __atomic_load_n(&sync->initialized, __ATOMIC_ACQUIRE) == 0U)
        return LV_RESULT_INVALID;
    __atomic_store_n(&sync->initialized, 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&sync->signaled, 1U, __ATOMIC_RELEASE);
    send_event();
    return LV_RESULT_OK;
}

void kshim_lvgl_os_thread_set_inline(lv_thread_t *thread)
{
    if (thread == NULL)
        return;
    *thread = (lv_thread_t){
        .worker_index = KSHIM_LVGL_NO_WORKER,
        .inline_mode = 1U,
    };
}

int kshim_lvgl_os_thread_is_inline(const lv_thread_t *thread)
{
    return thread != NULL && thread->inline_mode != 0U;
}

size_t kshim_lvgl_os_active_threads(void)
{
    return __atomic_load_n(&mActiveThreads, __ATOMIC_ACQUIRE);
}

size_t kshim_lvgl_os_peak_threads(void)
{
    return __atomic_load_n(&mPeakThreads, __ATOMIC_ACQUIRE);
}

uint32_t kshim_lvgl_os_worker_mask(void)
{
    return __atomic_load_n(&mReservedWorkers, __ATOMIC_ACQUIRE);
}

int kshim_lvgl_os_reset_stats(void)
{
    if (kshim_lvgl_os_active_threads() != 0U ||
        kshim_lvgl_os_worker_mask() != 0U)
        return -1;
    __atomic_store_n(&mPeakThreads, 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&mFailure, 0U, __ATOMIC_RELEASE);
    return 0;
}

int kshim_lvgl_os_failed(void)
{
    return __atomic_load_n(&mFailure, __ATOMIC_ACQUIRE) != 0U;
}

int kshim_lvgl_os_prepare_shutdown(void)
{
    return !kshim_lvgl_os_failed() &&
        kshim_lvgl_os_active_threads() == 0U &&
        kshim_lvgl_os_worker_mask() == 0U ? 0 : -1;
}

#endif
