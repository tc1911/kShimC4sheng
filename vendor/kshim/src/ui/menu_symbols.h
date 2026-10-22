#ifndef KSHIM_MENU_SYMBOLS_H
#define KSHIM_MENU_SYMBOLS_H
/* Procedural generic media and selection marks: no trademark assets or click targets. */
static inline void KshimSymbolLine(lv_event_t *e,uint32_t color,lv_opa_t opa,
    int32_t x1,int32_t y1,int32_t x2,int32_t y2,int32_t width,bool rounded) {
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.color=lv_color_hex(color); d.opa=opa; d.width=width;
    d.round_start=rounded; d.round_end=rounded;
    d.p1.x=x1; d.p1.y=y1; d.p2.x=x2; d.p2.y=y2;
    lv_draw_line(lv_event_get_layer(e),&d);
}
static inline void KshimSymbolRect(lv_event_t *e,uint32_t color,lv_opa_t opa,
    int32_t x,int32_t y,int32_t w,int32_t h,int32_t radius,int32_t stroke) {
    if(w<1 || h<1) return;
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.radius=radius;
    d.bg_color=lv_color_hex(color); d.bg_opa=stroke?LV_OPA_TRANSP:opa;
    d.border_color=lv_color_hex(color); d.border_width=stroke; d.border_opa=opa;
    lv_area_t a={x,y,x+w-1,y+h-1}; lv_draw_rect(lv_event_get_layer(e),&d,&a);
}
static inline void KshimSymbolCheck(lv_event_t *e,uint32_t color,lv_opa_t opa,
    int32_t x,int32_t y,int32_t s,int32_t stroke) {
    KshimSymbolLine(e,color,opa,x,y+s/2,x+s/3,y+s*5/6,stroke,true);
    KshimSymbolLine(e,color,opa,x+s/3,y+s*5/6,x+s,y+s/6,stroke,true);
}
static inline void KshimSymbolCorners(lv_event_t *e,uint32_t color,lv_opa_t opa,
    int32_t x,int32_t y,int32_t w,int32_t h,int32_t s,int32_t stroke) {
    if(w<2*s || h<2*s) return;
    KshimSymbolLine(e,color,opa,x,y,x+s,y,stroke,false);
    KshimSymbolLine(e,color,opa,x,y,x,y+s,stroke,false);
    KshimSymbolLine(e,color,opa,x+w-1-s,y,x+w-1,y,stroke,false);
    KshimSymbolLine(e,color,opa,x+w-1,y,x+w-1,y+s,stroke,false);
    KshimSymbolLine(e,color,opa,x,y+h-1-s,x,y+h-1,stroke,false);
    KshimSymbolLine(e,color,opa,x,y+h-1,x+s,y+h-1,stroke,false);
    KshimSymbolLine(e,color,opa,x+w-1-s,y+h-1,x+w-1,y+h-1,stroke,false);
    KshimSymbolLine(e,color,opa,x+w-1,y+h-1-s,x+w-1,y+h-1,stroke,false);
}
static inline void KshimSymbolMedia(lv_event_t *e,uint16_t kind,uint32_t ink,
    uint32_t surface,uint32_t accent,lv_opa_t opa,int32_t x,int32_t y,int32_t s) {
    int32_t stroke=s>=48?3:s>=24?2:1;
    if(kind==KSHIM_DESIGN_CUPERTINO || kind==KSHIM_DESIGN_CLOVER) {
        /* A neutral removable medium, not an inferred OS logo. */
        KshimSymbolRect(e,surface,opa,x,y,s,s, s/9,0);
        KshimSymbolRect(e,ink,opa,x,y,s,s,s/9,stroke);
        KshimSymbolLine(e,ink,opa,x+s/7,y+s*3/4,x+s*6/7,y+s*3/4,stroke,true);
        KshimSymbolRect(e,accent,opa,x+s*3/4,y+s*7/8,s/12,s/24+1,s/24,0);
        KshimSymbolRect(e,ink,opa,x+s/5,y+s/5,s*3/5,s/3,s/10,stroke);
    } else if(kind==KSHIM_DESIGN_HARMONYOS || kind==KSHIM_DESIGN_CARDS || kind==KSHIM_DESIGN_BENTO || kind==KSHIM_DESIGN_ONE_UI) {
        KshimSymbolRect(e,surface,opa,x,y,s,s,kind==KSHIM_DESIGN_HARMONYOS?s/3:s/4,0);
        int32_t ix=x+s/4, iy=y+s/4, is=s/2;
        KshimSymbolRect(e,ink,opa,ix,iy,is,is,is/8,stroke);
        KshimSymbolLine(e,ink,opa,ix+is/5,iy+is*3/4,ix+is*4/5,iy+is*3/4,stroke,true);
    } else {
        KshimSymbolRect(e,ink,opa,x+s/6,y,s*2/3,s,s/10,stroke);
        KshimSymbolLine(e,ink,opa,x+s/3,y+s*3/4,x+s*2/3,y+s*3/4,stroke,true);
    }
}
#endif
