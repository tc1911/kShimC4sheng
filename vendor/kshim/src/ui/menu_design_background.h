#ifndef KSHIM_MENU_DESIGN_BACKGROUND_H
#define KSHIM_MENU_DESIGN_BACKGROUND_H
/* Selected-only, one-time cached background. 57,600+480 bytes <= 64 KiB. */
#if ((defined(CONFIG_KSHIM_MENU_STYLE_GLASS) && CONFIG_KSHIM_MENU_STYLE_GLASS) || \
     (defined(CONFIG_KSHIM_MENU_STYLE_AURORA) && CONFIG_KSHIM_MENU_STYLE_AURORA)) && \
    (!defined(CONFIG_KSHIM_MENU_REDUCED_EFFECTS) || !CONFIG_KSHIM_MENU_REDUCED_EFFECTS)
#define KSHIM_DESIGN_BG_SIZE 120U
static uint32_t mDesignBackgroundPixels[KSHIM_DESIGN_BG_SIZE*KSHIM_DESIGN_BG_SIZE];
static uint32_t mDesignBackgroundLine[KSHIM_DESIGN_BG_SIZE];
static lv_image_dsc_t mDesignBackgroundImage;
_Static_assert(sizeof mDesignBackgroundPixels+sizeof mDesignBackgroundLine<=65536U,"background exceeds 64 KiB");
static uint32_t KshimDesignMix(uint32_t a,uint32_t b,unsigned t) {
    uint32_t r=0;
    for(unsigned shift=0;shift<=16;shift+=8)
        r|=((((a>>shift)&255U)*(255U-t)+((b>>shift)&255U)*t+127U)/255U)<<shift;
    return 0xff000000U|r;
}
#if defined(CONFIG_KSHIM_MENU_STYLE_GLASS) && CONFIG_KSHIM_MENU_STYLE_GLASS
static bool mDesignGlassDamagePending;
/* Keep cached-underlay sampling stable around transformed translucent content.
 * Expand actual pending damage before rendering, NOT INVALIDATE_AREA: LVGL
 * also uses that event to calculate partial-buffer strip heights. Never change
 * styles during drawing, force a refresh, or redraw an idle screen repeatedly. */
static void KshimDesignGlassRefresh(lv_event_t *event) {
    if(lv_event_get_code(event)==LV_EVENT_REFR_REQUEST) {
        mDesignGlassDamagePending=true;
        return;
    }
    if(!mDesignGlassDamagePending) return;
    lv_display_t *display=lv_event_get_target(event);
    lv_obj_t *screen=display?lv_display_get_screen_active(display):NULL;
    if(screen) lv_obj_invalidate(screen);
    /* The expansion itself also sends REFR_REQUEST; consume that request. */
    mDesignGlassDamagePending=false;
}
#endif
static bool KshimDesignBackground(lv_obj_t *image) {
    if(!image) return false;
    const int side=(int)KSHIM_DESIGN_BG_SIZE;
    const uint32_t colors[3]={mMenuDesign.background==2?0x326d65U:0x27658dU,
                             mMenuDesign.background==2?0x51406bU:0x495589U,
                             mMenuDesign.background==2?0x284b5eU:0x25505eU};
    const int cx[3]={15,97,70},cy[3]={27,43,109};
    for(int y=0;y<side;y++) for(int x=0;x<side;x++) {
        uint32_t c=KshimDesignMix(mMenuStyle.Screen,mMenuStyle.ScreenEnd,(unsigned)y*255U/(unsigned)(side-1));
        for(unsigned k=0;k<3;k++) {
            int dx=x-cx[k],dy=y-cy[k]; int energy=5000-dx*dx-dy*dy;
            if(energy>0) c=KshimDesignMix(c,colors[k],(unsigned)energy*150U/5000U);
        }
        mDesignBackgroundPixels[y*side+x]=c;
    }
    /* Separable radius-3 box filter; clamp edges and keep only one scanline. */
    for(unsigned pass=0;pass<2;pass++) for(int line=0;line<side;line++) {
        for(int t=0;t<side;t++) mDesignBackgroundLine[t]=mDesignBackgroundPixels[pass?t*side+line:line*side+t];
        for(int t=0;t<side;t++) {
            uint32_t r=0,g=0,b=0;
            for(int k=-3;k<=3;k++) {
                int idx=t+k; if(idx<0) idx=0; if(idx>=side) idx=side-1;
                uint32_t c=mDesignBackgroundLine[idx]; r+=(c>>16)&255U;g+=(c>>8)&255U;b+=c&255U;
            }
            mDesignBackgroundPixels[pass?t*side+line:line*side+t]=0xff000000U|(r/7U<<16)|(g/7U<<8)|b/7U;
        }
    }
    mDesignBackgroundImage=(lv_image_dsc_t){
        .header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,
            .w=KSHIM_DESIGN_BG_SIZE,.h=KSHIM_DESIGN_BG_SIZE,.stride=KSHIM_DESIGN_BG_SIZE*4U},
        .data_size=sizeof mDesignBackgroundPixels,
        .data=(const uint8_t *)(const void *)mDesignBackgroundPixels};
    lv_image_cache_drop(&mDesignBackgroundImage);
    lv_image_set_src(image,&mDesignBackgroundImage);
#if defined(CONFIG_KSHIM_MENU_STYLE_GLASS) && CONFIG_KSHIM_MENU_STYLE_GLASS
    mDesignGlassDamagePending=true;
    lv_display_t *display=lv_obj_get_display(image);
    lv_display_add_event_cb(display,KshimDesignGlassRefresh,LV_EVENT_REFR_REQUEST,NULL);
    lv_display_add_event_cb(display,KshimDesignGlassRefresh,LV_EVENT_REFR_START,NULL);
#endif
    return true;
}
#else
static inline bool KshimDesignBackground(lv_obj_t *image) { (void)image; return false; }
#endif
#endif
