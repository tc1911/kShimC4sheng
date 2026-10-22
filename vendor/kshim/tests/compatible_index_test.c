/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libfdt.h>

int dt_validate(const void *fdt, size_t available);
void dt_parent_index_begin(const void *fdt);
void dt_parent_index_end(void);
extern int dt_node_offset_by_compatible(const void *f, int start, const char *compat);

int __real_fdt_node_offset_by_compatible(const void *, int, const char *);
static int g_wrap;
int __wrap_fdt_node_offset_by_compatible(const void *f, int s, const char *c)
{ g_wrap++; return __real_fdt_node_offset_by_compatible(f, s, c); }
static int ora(const void *f, int s, const char *c)
{ return __real_fdt_node_offset_by_compatible(f, s, c); }

#define AVAIL (1u << 20)
static void *g_al[64]; static int g_nal;
static uint8_t *raw(size_t al, size_t sz)
{ void *p = NULL; assert(posix_memalign(&p, al, sz) == 0); memset(p, 0, sz);
  assert(g_nal < 64); g_al[g_nal++] = p; return p; }
static void raw_free_all(void) { while (g_nal) free(g_al[--g_nal]); }

static void w32(uint8_t *p, uint32_t v)
{ p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }

struct W { uint8_t *s; size_t so, sc; char *t; size_t sl, tc; };
static void wnew(struct W *w)
{ w->sc = 1u<<20; w->so = 0; w->s = malloc(w->sc); assert(w->s);
  w->tc = 1u<<14; w->sl = 0; w->t = malloc(w->tc); assert(w->t); }
static void wdel(struct W *w) { free(w->s); free(w->t); }
static void sw32(struct W *w, uint32_t v)
{ assert(w->so + 4 <= w->sc); w32(w->s + w->so, v); w->so += 4; }
static void wpad(struct W *w)
{ while (w->so & 3u) { assert(w->so < w->sc); w->s[w->so++] = 0; } }
static void wbeg(struct W *w, const char *n)
{ size_t l = strlen(n) + 1; sw32(w, FDT_BEGIN_NODE);
  assert(w->so + l <= w->sc); memcpy(w->s + w->so, n, l); w->so += l; wpad(w); }
static void wend(struct W *w) { sw32(w, FDT_END_NODE); }
static uint32_t wstr(struct W *w, const char *n)
{ for (size_t i = 0; i < w->sl;) { if (!strcmp(w->t + i, n)) return (uint32_t)i; i += strlen(w->t + i) + 1; }
  size_t l = strlen(n) + 1; assert(w->sl + l <= w->tc); memcpy(w->t + w->sl, n, l);
  uint32_t o = (uint32_t)w->sl; w->sl += l; return o; }
static void wprop(struct W *w, const char *n, const void *d, size_t l)
{ uint32_t no = wstr(w, n); sw32(w, FDT_PROP); sw32(w, (uint32_t)l); sw32(w, no);
  assert(w->so + l <= w->sc); memcpy(w->s + w->so, d, l); w->so += l; wpad(w); }
static void wps(struct W *w, const char *n, const char *v) { wprop(w, n, v, strlen(v) + 1); }
static void *finish(struct W *w)
{ sw32(w, FDT_END); size_t os = 56, ot = os + w->so; assert(ot + w->sl <= AVAIL);
  uint8_t *b = raw(AVAIL, AVAIL); memcpy(b + os, w->s, w->so); memcpy(b + ot, w->t, w->sl);
  w32(b+0, 0xd00dfeedu); w32(b+4, AVAIL); w32(b+8, (uint32_t)os); w32(b+12, (uint32_t)ot);
  w32(b+16, 40); w32(b+20, 17); w32(b+24, 16); w32(b+28, 0);
  w32(b+32, (uint32_t)w->sl); w32(b+36, (uint32_t)w->so); return b; }

static void *build_main(void)
{ struct W w; wnew(&w);
  wbeg(&w, ""); wps(&w, "compatible", "vendor,root");
  wbeg(&w, "disabledparent"); wps(&w, "status", "disabled");
  wbeg(&w, "kid"); wps(&w, "compatible", "vendor,a"); wend(&w); wend(&w);
  wbeg(&w, "multi");
  static const char list[] = "vendor,a\0vendor,b";
  wprop(&w, "compatible", list, sizeof(list)); wend(&w);
  wbeg(&w, "noprop"); wend(&w);
  wbeg(&w, "zero"); wprop(&w, "compatible", "", 0); wend(&w);
  wbeg(&w, "malformed"); wprop(&w, "compatible", "vendor,mal", 10); wend(&w);
  wbeg(&w, "final"); wps(&w, "compatible", "vendor,a"); wend(&w);
  wend(&w); void *b = finish(&w); wdel(&w); return b; }

static void *build_second(void)
{ struct W w; wnew(&w); wbeg(&w, ""); wps(&w, "compatible", "vendor,b");
  wend(&w); void *b = finish(&w); wdel(&w); return b; }

static void *build_many(int n)
{ struct W w; wnew(&w); wbeg(&w, ""); wps(&w, "compatible", "vendor,root");
  for (int i = 0; i < n; i++) { char nm[16]; (void)snprintf(nm, sizeof nm, "n%d", i);
    wbeg(&w, nm); wps(&w, "compatible", "vendor,a"); wend(&w); }
  wend(&w); void *b = finish(&w); wdel(&w); return b; }

static void *build_deep(int depth)
{ struct W w; wnew(&w); wbeg(&w, ""); wps(&w, "compatible", "vendor,root");
  for (int i = 0; i < depth; i++) { char nm[16]; (void)snprintf(nm, sizeof nm, "d%d", i); wbeg(&w, nm); }
  for (int i = 0; i < depth; i++) wend(&w);
  wend(&w); void *b = finish(&w); wdel(&w); return b; }

static void chk(const void *b, int s, const char *q, int active)
{ int before = g_wrap; int r = dt_node_offset_by_compatible(b, s, q);
  assert(r == ora(b, s, q)); assert(g_wrap == (active ? before : before + 1)); }

int main(void)
{ void *B = build_main(); assert(dt_validate(B, AVAIL) == 0);
  g_wrap = 0; chk(B, -1, "vendor,a", 0); assert(g_wrap == 1);
  int offs[64], no = 0, d = -1, o = -1;
  for (;;) { int n = fdt_next_node(B, o, &d); if (n < 0 || d < 0) break; o = n; assert(no < 64); offs[no++] = n; }
  assert(no >= 6);
  dt_parent_index_begin(B);
  static const char *qs[] = { "vendor,a", "vendor,b", "vendor,root", "missing", "" };
  g_wrap = 0;
  for (int qi = 0; qi < 5; qi++) { chk(B, -1, qs[qi], 1); chk(B, -2, qs[qi], 1);
    for (int k = 0; k < no; k++) chk(B, offs[k], qs[qi], 1); }
  assert(g_wrap == 0);
  chk(B, 1, "vendor,a", 0); chk(B, INT_MAX, "vendor,a", 0); assert(g_wrap == 2);
  void *B2 = build_second(); assert(dt_validate(B2, AVAIL) == 0);
  chk(B2, -1, "vendor,b", 0); assert(g_wrap == 3);
  dt_parent_index_end(); chk(B, -1, "vendor,a", 0); assert(g_wrap == 4);
  assert(fdt_open_into(B, B, AVAIL) == 0);
  int root = fdt_path_offset(B, "/"); assert(root >= 0);
  assert(fdt_setprop_string(B, root, "compatible", "vendor,new") == 0);
  assert(dt_validate(B, AVAIL) == 0);
  dt_parent_index_begin(B); g_wrap = 0;
  chk(B, -1, "vendor,new", 1); chk(B, -1, "vendor,root", 1);
  assert(dt_node_offset_by_compatible(B, -1, "vendor,root") == -1);
  dt_parent_index_end();
  void *B3 = build_many(9000); assert(dt_validate(B3, AVAIL) == 0);
  dt_parent_index_begin(B3); g_wrap = 0;
  int r3 = dt_node_offset_by_compatible(B3, -1, "vendor,a");
  assert(r3 == ora(B3, -1, "vendor,a")); assert(r3 >= 0); assert(g_wrap == 1);
  dt_parent_index_end();
  void *B4 = build_deep(70); assert(dt_validate(B4, AVAIL) == 0);
  dt_parent_index_begin(B4); g_wrap = 0;
  int r4 = dt_node_offset_by_compatible(B4, -1, "vendor,root");
  assert(r4 == ora(B4, -1, "vendor,root")); assert(r4 >= 0); assert(g_wrap == 1);
  dt_parent_index_end();
  raw_free_all(); return 0; }
