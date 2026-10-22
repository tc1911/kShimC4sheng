#ifndef KSHIM_MENU_DESIGN_FONT_H
#define KSHIM_MENU_DESIGN_FONT_H
/* Fixed-advance ASCII adapter over the repository font. CJK falls back unchanged.
 * This is not a bundled monospace font; the glyph outlines are the existing font.
 * LVGL overwrites resolved_font after get_glyph_dsc, so bitmap/release delegation
 * must temporarily restore the underlying font. No glyph cache ownership changes. */
#if defined(CONFIG_KSHIM_MENU_STYLE_TERMINAL) && CONFIG_KSHIM_MENU_STYLE_TERMINAL
static lv_font_t mDesignMonoFonts[3];
typedef struct { const lv_font_t *base; uint16_t cell; } kshim_design_mono_t;
static kshim_design_mono_t mDesignMonoInfo[3];
static bool KshimMonoGlyph(const lv_font_t *font,lv_font_glyph_dsc_t *d,uint32_t letter,uint32_t next) {
    (void)next;
    const kshim_design_mono_t *info=font->user_data;
    if(letter<32U || letter>126U || !info || !info->base) return false;
    if(!info->base->get_glyph_dsc(info->base,d,letter,0U)) return false;
    d->adv_w=info->cell;
    d->ofs_x=(int16_t)(((int32_t)info->cell-(int32_t)d->box_w)/2);
    return true;
}
static const void *KshimMonoBitmap(lv_font_glyph_dsc_t *d,lv_draw_buf_t *buffer) {
    const lv_font_t *wrapper=d->resolved_font;
    const kshim_design_mono_t *info=wrapper->user_data;
    d->resolved_font=info->base;
    const void *result=lv_font_get_glyph_bitmap(d,buffer);
    d->resolved_font=wrapper;
    return result;
}
static void KshimMonoRelease(const lv_font_t *font,lv_font_glyph_dsc_t *d) {
    const kshim_design_mono_t *info=font->user_data;
    d->resolved_font=info->base;
    lv_font_glyph_release_draw_data(d);
    d->resolved_font=font;
}
static const lv_font_t *KshimMonoWrap(unsigned index,const lv_font_t *base) {
    if(!base || index>=3U) return base;
    uint16_t cell=1;
    for(uint32_t c=32;c<=126;c++) {
        lv_font_glyph_dsc_t d;
        if(lv_font_get_glyph_dsc(base,&d,c,0U)) {
            uint16_t width=d.box_w>d.adv_w?d.box_w:d.adv_w;
            if(width>cell) cell=width;
        }
    }
    if(cell<UINT16_MAX-2U) cell+=2U;
    mDesignMonoInfo[index]=(kshim_design_mono_t){base,cell};
    mDesignMonoFonts[index]=*base;
    mDesignMonoFonts[index].get_glyph_dsc=KshimMonoGlyph;
    mDesignMonoFonts[index].get_glyph_bitmap=KshimMonoBitmap;
    mDesignMonoFonts[index].release_glyph=KshimMonoRelease;
    mDesignMonoFonts[index].fallback=base;
    mDesignMonoFonts[index].kerning=LV_FONT_KERNING_NONE;
    mDesignMonoFonts[index].user_data=&mDesignMonoInfo[index];
    return &mDesignMonoFonts[index];
}
static void KshimDesignFonts(void) {
    mTitleFont=KshimMonoWrap(0,mTitleFont);
    mRowFont=KshimMonoWrap(1,mRowFont);
    mStatusFont=KshimMonoWrap(2,mStatusFont);
}
static void KshimDesignForgetFonts(void) {
    memset(mDesignMonoFonts,0,sizeof mDesignMonoFonts);
    memset(mDesignMonoInfo,0,sizeof mDesignMonoInfo);
}
#else
static inline void KshimDesignFonts(void) {}
static inline void KshimDesignForgetFonts(void) {}
#endif
#endif
