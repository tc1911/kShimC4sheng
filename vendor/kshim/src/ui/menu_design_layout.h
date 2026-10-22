#ifndef KSHIM_MENU_DESIGN_LAYOUT_H
#define KSHIM_MENU_DESIGN_LAYOUT_H
/* Pure geometry, no LVGL or framebuffer allocation. Units are not physical dp/pt. */
#include "menu_design.h"
#include <string.h>
#if defined(CONFIG_KSHIM_MENU_LANGUAGE_SWITCH) && CONFIG_KSHIM_MENU_LANGUAGE_SWITCH
#define KSHIM_DESIGN_MAX_ENTRIES 53U
#else
#define KSHIM_DESIGN_MAX_ENTRIES 51U
#endif

typedef struct { int32_t x,y,w,h; } kshim_design_rect_t;
typedef struct {
    kshim_design_rect_t panel,title,status,list,boot,detail,notice,footer,credit,language,illustration;
    kshim_design_rect_t items[KSHIM_DESIGN_MAX_ENTRIES];
    int32_t content_width,content_height,row_height,icon_size,gutter,gap;
    uint16_t scale_q8;
    uint8_t count,columns,horizontal,split,compact;
} kshim_design_geometry_t;
static inline int32_t kshim_design_min(int32_t a,int32_t b) { return a<b?a:b; }
static inline int32_t kshim_design_max(int32_t a,int32_t b) { return a>b?a:b; }
static inline int32_t kshim_design_px(uint32_t n,uint32_t q) { return (int32_t)((n*q+128U)/256U); }
static inline int32_t kshim_design_stroke(uint32_t n,uint32_t q) { return n?kshim_design_max(1,kshim_design_px(n,q)):0; }
static inline uint16_t kshim_design_scale(uint32_t w,uint32_t h,uint32_t override) {
    uint32_t q=override;
    if(q==0U) { q=(w<h?w:h)*256U/720U; if(q<256U) q=256U; }
    if(q<96U) q=96U;
    if(q>512U) q=512U;
    return (uint16_t)q;
}
static inline void kshim_design_compact(const kshim_menu_design_t *d,
    uint32_t w,uint32_t h,uint32_t n,kshim_design_geometry_t *g) {
    int32_t margin=kshim_design_max(2,kshim_design_min((int32_t)w,(int32_t)h)/32);
    g->panel=(kshim_design_rect_t){margin,margin,(int32_t)w-2*margin,(int32_t)h-2*margin};
    int32_t p=kshim_design_max(2,margin/2), bh=kshim_design_min(32,g->panel.h/4);
    g->compact=1; g->columns=1; g->horizontal=0; g->split=0; g->icon_size=0;
    g->gutter=0; g->gap=0; g->title=(kshim_design_rect_t){0};
    g->status=(kshim_design_rect_t){0}; g->detail=(kshim_design_rect_t){0};
    g->notice=(kshim_design_rect_t){0}; g->footer=(kshim_design_rect_t){0};
    g->credit=(kshim_design_rect_t){0}; g->language=(kshim_design_rect_t){0};
    g->illustration=(kshim_design_rect_t){0};
    g->boot=(kshim_design_rect_t){p,g->panel.h-p-bh,g->panel.w-2*p,bh};
    g->list=(kshim_design_rect_t){p,p,g->panel.w-2*p,g->boot.y-2*p};
    g->row_height=kshim_design_max(12,kshim_design_min(kshim_design_px(d->row_height,g->scale_q8),g->list.h));
    for(uint32_t i=0;i<n;i++) g->items[i]=(kshim_design_rect_t){0,(int32_t)i*g->row_height,g->list.w,g->row_height};
    g->content_width=g->list.w;
    g->content_height=kshim_design_max(g->list.h,(int32_t)n*g->row_height);
}
/* STAR_CHART is a wide two-column star chart: navigation on the left, a live
 * illustration and the selected entry's description on the right. Isolated so
 * the shared list/grid path above stays untouched. Pure integers, no LVGL. */
static inline int kshim_design_star_chart(const kshim_menu_design_t *d,
    uint32_t w,uint32_t h,uint32_t n,uint32_t q,kshim_design_geometry_t *g) {
    g->columns=1; g->split=1; g->horizontal=0; g->compact=0;
    g->icon_size=kshim_design_px(36U,q);
    /* Portrait or cramped landscape keeps the shared compact fallback; the
     * added credit/language/illustration rects stay zeroed by the caller. */
    if(w<h || w<800U || h<450U) { kshim_design_compact(d,w,h,n,g); return 0; }
    int32_t margin=kshim_design_min((int32_t)w,(int32_t)h)/25;
    g->panel=(kshim_design_rect_t){margin,margin,(int32_t)w-2*margin,(int32_t)h-2*margin};
    int32_t p=kshim_design_px(20U,q);
    int32_t contentw=g->panel.w-2*p;
    int32_t section=kshim_design_px(24U,q);
    int32_t feather=kshim_design_px(80U,q);
    int32_t titleh=kshim_design_max(kshim_design_px(70U,q),
        kshim_design_px(d->title_size,q)*3/2+4);
    g->title=(kshim_design_rect_t){p+feather,p,contentw*70/100-feather,titleh};
    g->language=(kshim_design_rect_t){p+contentw*76/100,p+kshim_design_px(10U,q),
        contentw*24/100,kshim_design_px(44U,q)};
    int32_t liststart=p+titleh+section;
    int32_t footery=g->panel.h-p-kshim_design_px(80U,q);
    g->footer=(kshim_design_rect_t){p,footery,contentw*46/100,kshim_design_px(35U,q)};
    g->credit=(kshim_design_rect_t){p,g->panel.h-p-kshim_design_px(32U,q),contentw,
        kshim_design_px(32U,q)};
    int32_t listw=contentw*44/100;
    int32_t listh=footery-kshim_design_px(32U,q)-liststart;
    /* Cramped landscape or a forced high scale can shrink the list viewport
     * below a usable size; fall back to the shared compact path there. */
    if(listh<kshim_design_px(360U,q) || listw<kshim_design_px(160U,q)) {
        kshim_design_compact(d,w,h,n,g); return 0;
    }
    g->list=(kshim_design_rect_t){p,liststart,listw,listh};
    g->gap=kshim_design_px(6U,q);
    g->gutter=kshim_design_px(4U,q);
    int32_t rowfontline=kshim_design_px(28U,q)*3/2+2;
    int32_t rh=kshim_design_min(kshim_design_px(70U,q),(listh-6*g->gap-2*g->gutter)/7);
    int32_t minrh=rowfontline+kshim_design_px(8U,q);
    if(rh<minrh) rh=minrh; /* Short lists scroll rather than clip the label. */
    g->row_height=rh;
    for(uint32_t i=0;i<n;i++)
        g->items[i]=(kshim_design_rect_t){g->gutter,g->gutter+(int32_t)i*(rh+g->gap),
            listw-2*g->gutter,rh};
    int32_t lastend=g->gutter+(int32_t)n*rh+(int32_t)(n-1)*g->gap+g->gutter;
    g->content_width=g->list.w;
    g->content_height=kshim_design_max(g->list.h,lastend);
    int32_t rx=p+contentw*52/100, rw=contentw*48/100;
    int32_t maxheight=kshim_design_max(0,listh-kshim_design_px(320U,q));
    int32_t illh=kshim_design_min(kshim_design_min(g->panel.h*30/100,kshim_design_px(300U,q)),maxheight);
    g->illustration=(kshim_design_rect_t){rx,liststart,rw,illh};
    int32_t statush=kshim_design_px(54U,q);
    int32_t statusy=liststart+illh+kshim_design_px(18U,q);
    g->status=(kshim_design_rect_t){rx,statusy,rw,statush};
    g->detail=(kshim_design_rect_t){rx,statusy+statush+kshim_design_px(14U,q),rw,
        kshim_design_max(0,kshim_design_px(130U,q))};
    int32_t booth=kshim_design_px(68U,q);
    int32_t bootw=kshim_design_min(kshim_design_px(240U,q),rw);
    int32_t booty=g->list.y+g->list.h-booth;
    g->boot=(kshim_design_rect_t){rx+rw-bootw,booty,bootw,booth};
    int32_t noticeh=kshim_design_px(50U,q);
    g->notice=(kshim_design_rect_t){rx,booty-kshim_design_px(20U,q)-noticeh,rw,noticeh};
    /* The description may not crowd the countdown region. */
    int32_t detail_limit=g->notice.y-kshim_design_px(12U,q);
    if(g->detail.y+g->detail.h>detail_limit)
        g->detail.h=kshim_design_max(0,detail_limit-g->detail.y);
    return 0;
}
static inline int kshim_design_layout(const kshim_menu_design_t *d,
    uint32_t w,uint32_t h,uint32_t n,uint32_t scale_override,kshim_design_geometry_t *g) {
    if(!d || !g || !d->enabled || w<64U || h<48U || w>32768U || h>32768U || !n || n>KSHIM_DESIGN_MAX_ENTRIES) return -1;
    memset(g,0,sizeof *g);
    g->count=(uint8_t)n; g->scale_q8=kshim_design_scale(w,h,scale_override);
    const uint32_t q=g->scale_q8;
    if(d->kind==KSHIM_DESIGN_STAR_CHART) return kshim_design_star_chart(d,w,h,n,q,g);
    int32_t s=(int32_t)(w<h?w:h);
    int32_t margin=kshim_design_min(kshim_design_px(d->padding,q),kshim_design_max(2,s/24));
    int32_t p=kshim_design_px(d->padding,q);
    g->panel.w=kshim_design_min((int32_t)w-2*margin,kshim_design_px(d->max_width,q));
    int32_t cw=g->panel.w-2*p, cap=(int32_t)h-2*margin;
    if(w<280U || h<180U || cw<kshim_design_px(160,q)) { kshim_design_compact(d,w,h,n,g); return 0; }
    g->columns=1;
    if(d->layout==KSHIM_LAYOUT_GRID || d->layout==KSHIM_LAYOUT_BENTO) {
        if(cw>=kshim_design_px(d->grid_two,q)) g->columns=2;
        if(d->layout!=KSHIM_LAYOUT_BENTO && cw>=kshim_design_px(d->grid_three,q)) g->columns=3;
    }
    g->horizontal=d->layout==KSHIM_LAYOUT_RIBBON && w>=320U && h>=240U;
    g->split=d->layout==KSHIM_LAYOUT_SPLIT && w>h && w>=(uint32_t)kshim_design_px(d->kind==KSHIM_DESIGN_ONE_UI?600:800,q);
    g->icon_size=kshim_design_px(d->icon_size,q);
    g->gap=kshim_design_px(d->gap,q);
    g->gutter=kshim_design_px(kshim_design_max((int32_t)d->row_shadow,(int32_t)d->focus_glow/2+(int32_t)d->focus_outline+2),q);
    if(d->layout==KSHIM_LAYOUT_INSET) g->gutter=0;
    int32_t font_line=kshim_design_max(12,kshim_design_px(d->row_size,q))*3/2+2;
    g->row_height=kshim_design_max(kshim_design_px(d->row_height,q),(font_line*3+1)/2);
    int32_t th=kshim_design_max(20,kshim_design_max(12,kshim_design_px(d->title_size,q))*3/2+2);
    int32_t sh=kshim_design_max(20,kshim_design_max(10,kshim_design_px(d->status_size,q))*3/2+1);
    int32_t bh=kshim_design_px(d->boot_height,q), section_gap=kshim_design_px(16,q);
    int32_t status_gap=kshim_design_px(6,q);
    int32_t fixed=2*p+th+sh+status_gap+section_gap*2+bh;
    int32_t rh=(g->columns>1 || g->horizontal)?kshim_design_px(d->tile_height,q):g->row_height;
    if(fixed+rh+g->gutter*2>cap) { fixed-=sh+status_gap; sh=0; status_gap=0; }
    if(fixed+rh+g->gutter*2>cap) { fixed-=th+section_gap; th=0; }
    if(fixed+rh+g->gutter*2>cap) { kshim_design_compact(d,w,h,n,g); return 0; }
    int32_t rows=((int32_t)n+g->columns-1)/g->columns;
    if(d->layout==KSHIM_LAYOUT_BENTO && g->columns==2) {
        rows=0; int col=0;
        for(uint32_t i=0;i<n;i++) {
            if(i%5U==0U) { if(col) {rows++;col=0;} rows++; }
            else { col++; if(col==2) {rows++;col=0;} }
        }
        if(col) rows++;
    }
    if(g->horizontal) {
        rows=1;
        /* The selected name moves below the strip with a section gap, not
         * the smaller title/status gap. Reserve that difference before the
         * viewport-fit check or every large Clover display falls back. */
        if(sh) fixed+=section_gap-status_gap;
    }
    int32_t natural=fixed+rows*rh+(rows-1)*g->gap+2*g->gutter;
    int details_split=0;
#if defined(CONFIG_KSHIM_MENU_ENTRY_DETAILS) && CONFIG_KSHIM_MENU_ENTRY_DETAILS
    details_split=g->split && d->kind==KSHIM_DESIGN_SURFACE;
    if(details_split) {
        int32_t help_h=sh?sh:kshim_design_max(20,kshim_design_px(d->status_size,q)*3/2+1);
        int32_t nav_h=rows*rh+(rows-1)*g->gap+2*g->gutter+section_gap+help_h;
        /* Reserve three body lines and two notification lines beside the
         * real row extent. Small screens cap this and scroll only the list. */
        int32_t detail_h=font_line+section_gap+3*help_h+section_gap+2*help_h+section_gap+bh;
        natural=2*p+th+section_gap+kshim_design_max(nav_h,detail_h);
    }
#endif
    g->panel.h=kshim_design_min(natural,cap);
    if(g->split && !details_split) g->panel.h=kshim_design_min(cap,kshim_design_max(g->panel.h,(int32_t)h*3/4));
    g->panel.x=((int32_t)w-g->panel.w)/2;
    g->panel.y=((int32_t)h-g->panel.h)/2;
    g->title=(kshim_design_rect_t){p,p,cw,th};
    g->status=(kshim_design_rect_t){p,p+th+status_gap,cw,sh};
    int32_t ly=p+th+(th?section_gap:0)+sh+status_gap;
    int32_t by=g->panel.h-p-bh;
    g->list=(kshim_design_rect_t){p,ly,cw,by-section_gap-ly};
    int32_t bw=kshim_design_min(cw,kshim_design_px(d->boot_width,q));
    int32_t bx=d->boot_align==1?(g->panel.w-bw)/2:d->boot_align==2?p+cw-bw:p;
    g->boot=(kshim_design_rect_t){bx,by,bw,bh};
    if(g->split) {
        int32_t nav=cw*2/5;
        int32_t rx=p+nav+section_gap, rw=cw-nav-section_gap;
        g->list=(kshim_design_rect_t){p,p+th+section_gap,nav,g->panel.h-2*p-th-section_gap};
        g->status=(kshim_design_rect_t){rx,g->list.y,rw,kshim_design_max(20,kshim_design_max(12,kshim_design_px(d->title_size,q))*3/2+2)};
        g->detail=(kshim_design_rect_t){rx,g->status.y+g->status.h+section_gap,rw,
            by-section_gap-g->status.y-g->status.h-section_gap};
        if(g->detail.h<1) g->detail.h=0;
        g->boot.x=rx; g->boot.w=kshim_design_min(bw,rw);
#if defined(CONFIG_KSHIM_MENU_ENTRY_DETAILS) && CONFIG_KSHIM_MENU_ENTRY_DETAILS
        if(d->kind==KSHIM_DESIGN_SURFACE) {
            /* Keep navigation, persistent help, and transient notices in
             * separate regions; scrolling only moves the navigation rows. */
            int32_t help_h=sh?sh:kshim_design_max(20,kshim_design_px(d->status_size,q)*3/2+1);
            g->footer=(kshim_design_rect_t){p,g->panel.h-p-help_h,nav,help_h};
            g->list.h=g->footer.y-section_gap-g->list.y;
            g->status.h=font_line;
            g->notice=(kshim_design_rect_t){rx,by-section_gap-2*help_h,rw,2*help_h};
            g->detail.y=g->status.y+g->status.h+section_gap;
            g->detail.h=kshim_design_max(0,g->notice.y-section_gap-g->detail.y);
        }
#endif
    }
    /* Clover selection label is below the strip, not another heading above it. */
    if(g->horizontal && sh) {
        g->list.y=p+th+(th?section_gap:0);
        g->list.h=by-section_gap-sh-section_gap-g->list.y;
        g->status=(kshim_design_rect_t){p,g->list.y+g->list.h+section_gap,cw,sh};
    }
    /* One UI: a portrait viewing area above a reachable interaction area.
     * Large entry sets use all available list space; focus never moves chrome. */
    if(d->kind==KSHIM_DESIGN_ONE_UI && h>w && h>=(uint32_t)kshim_design_px(600,q) && th && sh) {
        g->panel.h=cap; g->panel.y=margin;
        g->boot.y=cap-p-bh;
        int32_t min_top=p+th+sh+status_gap+section_gap;
        int32_t available=g->boot.y-section_gap-min_top;
        int32_t requested=(int32_t)n*rh+((int32_t)n-1)*g->gap+2*g->gutter;
        int32_t limit=n>7U?available:kshim_design_min(available,cap*55/100);
        g->list.h=kshim_design_max(rh+2*g->gutter,kshim_design_min(requested,limit));
        g->list.y=g->boot.y-section_gap-g->list.h;
        int32_t title_y=n>7U?p:kshim_design_max(p,kshim_design_min(cap/5,g->list.y-th-sh-status_gap-section_gap));
        g->title.y=title_y;
        g->status.y=title_y+th+status_gap;
    }
    /* Metro is a page of typography, not a centered modal. */
    if(d->kind==KSHIM_DESIGN_METRO) g->panel.y=margin;
    int32_t inner=g->list.w-2*g->gutter;
    int32_t tilew=g->horizontal?kshim_design_px(d->tile_width,q):(inner-(g->columns-1)*g->gap)/g->columns;
    if(g->list.h<rh+2*g->gutter || tilew<1) { kshim_design_compact(d,w,h,n,g); return 0; }
    int32_t row=0,col=0,endx=0,endy=0;
    int32_t lead=g->gutter;
    if(g->horizontal) {
        int32_t extent=(int32_t)n*tilew+((int32_t)n-1)*g->gap;
        if(extent<inner) lead+=(inner-extent)/2;
    }
    for(uint32_t i=0;i<n;i++) {
        int span=d->layout==KSHIM_LAYOUT_BENTO && g->columns==2 && i%5U==0U?2:1;
        if(!g->horizontal && col+span>g->columns) {row++;col=0;}
        int32_t ix=lead+col*(tilew+g->gap), iy=g->gutter+row*(rh+g->gap);
        int32_t iw=span==2?inner:tilew;
        g->items[i]=(kshim_design_rect_t){ix,iy,iw,rh};
        endx=kshim_design_max(endx,ix+iw+g->gutter); endy=kshim_design_max(endy,iy+rh+g->gutter);
        col+=span;
        if(!g->horizontal && col>=g->columns) {row++;col=0;}
    }
    g->content_width=kshim_design_max(g->list.w,endx);
    g->content_height=kshim_design_max(g->list.h,endy);
    return 0;
}

/* Label/icon/selection reservations are shared by real drawing and host tests. */
typedef struct { kshim_design_rect_t label,icon,mark,prefix; } kshim_design_item_geometry_t;
static inline void kshim_design_item_layout(const kshim_menu_design_t *d,
    const kshim_design_geometry_t *g,unsigned index,int32_t line_height,kshim_design_item_geometry_t *o) {
    memset(o,0,sizeof *o);
    if(index>=g->count) return;
    int32_t w=g->items[index].w,h=g->items[index].h;
    int32_t p=kshim_design_min(kshim_design_px(12,g->scale_q8),kshim_design_min(w/12,h/6));
    if(p<1) p=1;
    if(line_height<1) line_height=1;
    int32_t lh=kshim_design_min(line_height,h-2*p);
    if(lh<1) lh=1;
    o->label=(kshim_design_rect_t){p,(h-lh)/2,kshim_design_max(1,w-2*p),lh};
    if(g->compact || w<kshim_design_px(140,g->scale_q8)) return;
    int tall=g->columns>1 || g->horizontal;
    int32_t ms=kshim_design_min(kshim_design_px(18,g->scale_q8),h-2*p);
    int leading=d->mark==KSHIM_MARK_BAR || d->mark==KSHIM_MARK_PILL || d->mark==KSHIM_MARK_DOT || d->mark==KSHIM_MARK_CURSOR;
    if(!tall) {
        int32_t x=p, right=w-p;
        if(leading) {
            int32_t markw=kshim_design_min(kshim_design_px(8,g->scale_q8),ms);
            o->mark=(kshim_design_rect_t){x,(h-ms)/2,markw,ms}; x+=markw+p;
        } else {
            o->mark=(kshim_design_rect_t){right-ms,(h-ms)/2,ms,ms}; right-=ms+p;
        }
        if(d->kind==KSHIM_DESIGN_TERMINAL) {
            int32_t pw=kshim_design_max(kshim_design_px(42,g->scale_q8),line_height*2);
            if(right-x>=pw+2*p+line_height) {
                o->prefix=(kshim_design_rect_t){x,(h-lh)/2,pw,lh}; x+=pw+p;
            }
        }
        int32_t is=kshim_design_min(g->icon_size,h-2*p);
        if(is>0 && right-x>is+3*p+line_height) {
            o->icon=(kshim_design_rect_t){x,(h-is)/2,is,is}; x+=is+p;
        }
        o->label.x=x; o->label.w=kshim_design_max(1,right-x);
    } else {
        int32_t is=kshim_design_min(g->icon_size,h-2*p-lh-p);
        if(is<0) is=0;
        int centered=d->kind==KSHIM_DESIGN_CUPERTINO || d->kind==KSHIM_DESIGN_CLOVER;
        int32_t iy=p, ix=centered?(w-is)/2:p;
        if(is>0) o->icon=(kshim_design_rect_t){ix,iy,is,is};
        int32_t room=h-2*p-(is?is+p:0);
        int32_t text_h=kshim_design_min(line_height*2,room);
        if(text_h<1) text_h=1;
        o->label=(kshim_design_rect_t){p,h-p-text_h,w-2*p,text_h};
        if(d->kind==KSHIM_DESIGN_CLOVER && is>0) {
            int32_t pad=kshim_design_min(p/2,(w-is)/2);
            o->mark=(kshim_design_rect_t){ix-pad,iy-pad,is+2*pad,is+2*pad};
        } else if(w-p-ms>ix+is+p) o->mark=(kshim_design_rect_t){w-p-ms,p,ms,ms};
        else if(!centered && is>0) o->mark=(kshim_design_rect_t){w-p-ms,p,ms,ms};
    }
}
#endif
