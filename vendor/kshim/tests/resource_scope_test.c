/* Resource scope lifecycle regression: real src/dt/resources.c + real dt/libfdt. */
#include <qcom_dt.h>
#include <qcom_key_platform.h>
#include <string.h>
#include <stdio.h>

static char blob[1024] __attribute__((aligned(8)));
static int g_leaf;
static int spmi_calls, inject_failure, parent_calls, spmi_bad;
static unsigned bind_calls;

int __real_fdt_parent_offset(const void *fdt, int offset);
int __wrap_fdt_parent_offset(const void *fdt, int offset)
{
    parent_calls++;
    return __real_fdt_parent_offset(fdt, offset);
}

int kshim_dt_spmi(const void *fdt, struct kshim_resources *r)
{
    spmi_calls++;
    int before = parent_calls;
    if (dt_parent_offset(fdt, g_leaf) != __real_fdt_parent_offset(fdt, g_leaf)) spmi_bad = 2;
    if (parent_calls != before) spmi_bad = 1;
    r->spmi_count = 1;
    memset(&r->spmi[0].controller, 0, sizeof(r->spmi[0].controller));
    SpmiMemoryRegion *m = &r->spmi[0].controller.Regions[0];
    if (inject_failure) { m->BaseAddress = UINT64_MAX; m->Size = 2; }
    else { m->BaseAddress = 0x10000; m->Size = 4096; }
    return 0;
}

void kshim_qcom_spmi_bind(SpmiDeviceContext **contexts, unsigned count)
{
    (void)contexts; (void)count; bind_calls++;
}

#define CHECK(c) do { if (!(c)) { printf("FAIL %d %s\n", __LINE__, #c); return 1; } } while (0)

int main(void)
{
    CHECK(fdt_create_empty_tree(blob, sizeof(blob)) == 0);
    int a = fdt_add_subnode(blob, 0, "a");
    CHECK(a >= 0);
    int b = fdt_add_subnode(blob, a, "b");
    CHECK(b >= 0);
    g_leaf = fdt_add_subnode(blob, b, "leaf");
    CHECK(g_leaf >= 0);
    CHECK(fdt_pack(blob) == 0);
    CHECK(dt_validate(blob, sizeof(blob)) == 0);

    /* Failure: parent index cache is active INSIDE the parser and INACTIVE (cleared) after kshim_resources_init returns. */
    inject_failure = 1;
    CHECK(kshim_resources_init(blob, sizeof(blob)) == -1);
    int before = parent_calls;
    dt_parent_offset(blob, g_leaf);
    CHECK(parent_calls == before + 1);

    /* Success: same as failure - cache is active INSIDE the parser and INACTIVE after kshim_resources_init returns. */
    inject_failure = 0;
    CHECK(kshim_resources_init(blob, sizeof(blob)) == 0);
    CHECK(kshim_resources_get()->mapping_count == 1);
    before = parent_calls;
    dt_parent_offset(blob, g_leaf);
    CHECK(parent_calls == before + 1);

    /* Idempotent init: no rebuild, no spmi re-run; cache is active INSIDE the parser and INACTIVE after kshim_resources_init returns. */
    int calls = spmi_calls;
    CHECK(kshim_resources_init(blob, sizeof(blob)) == 0);
    CHECK(spmi_calls == calls);
    before = parent_calls;
    dt_parent_offset(blob, g_leaf);
    CHECK(parent_calls == before + 1);

    /* Invalid size: no cache built, real fdt_parent_offset is consulted. */
    CHECK(kshim_resources_init(blob, 0) == -1);
    before = parent_calls;
    dt_parent_offset(blob, g_leaf);
    CHECK(parent_calls == before + 1);

    CHECK(spmi_bad == 0);
    CHECK(spmi_calls == 2);
    CHECK(bind_calls == 1);
    printf("PASS\n");
    return 0;
}
