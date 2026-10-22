#ifndef KSHIM_MENU_STAR_CHART_H
#define KSHIM_MENU_STAR_CHART_H
/* Midnight star-chart hand-drawn outline layer for KSHIM_DESIGN_STAR_CHART.
 * Include from menu_design_port.h after KshimDesignSolid.
 * Outline primitives only (line + outlined rounded rect/circle): no images,
 * fonts, timers, animation, randomness, trigonometry, extra LVGL objects, or heap. */

typedef struct { int32_t x,y; } kshim_star_pt_t;

static inline kshim_design_rect_t KshimStarInset(kshim_design_rect_t r,int32_t i) {
    r.x+=i; r.y+=i; r.w-=2*i; r.h-=2*i;
    if(r.w<1) r.w=0;
    if(r.h<1) r.h=0;
    return r;
}
static inline kshim_design_rect_t KshimStarShift(kshim_design_rect_t r,int32_t dx,int32_t dy) {
    r.x+=dx; r.y+=dy; return r;
}
static inline kshim_design_rect_t KshimStarSub(kshim_design_rect_t r,int32_t xn,int32_t yn,int32_t wn,int32_t hn) {
    kshim_design_rect_t o;
    o.x=r.x+r.w*xn/1000; o.y=r.y+r.h*yn/1000;
    o.w=r.w*wn/1000; o.h=r.h*hn/1000;
    if(o.w<1) o.w=1;
    if(o.h<1) o.h=1;
    return o;
}
static inline int32_t KshimStarMapX(kshim_design_rect_t r,int32_t n) { return r.x+(r.w-1)*n/1000; }
static inline int32_t KshimStarMapY(kshim_design_rect_t r,int32_t n) { return r.y+(r.h-1)*n/1000; }
static inline int32_t KshimStarMapR(kshim_design_rect_t r,int32_t n) {
    int32_t m=r.w<r.h?r.w:r.h; return n*m/1000;
}
/* Fit a fixed wnum:hnum canvas centred inside r (integer math, never stretched). */
static inline kshim_design_rect_t KshimStarAspect(kshim_design_rect_t r,int32_t wnum,int32_t hnum) {
    int32_t cw,ch;
    if(r.w<1 || r.h<1 || wnum<1 || hnum<1) return r;
    cw=r.w; ch=cw*hnum/wnum;
    if(ch>r.h) { ch=r.h; cw=ch*wnum/hnum; }
    if(cw<1) cw=1;
    if(ch<1) ch=1;
    r.x+=(r.w-cw)/2; r.y+=(r.h-ch)/2; r.w=cw; r.h=ch;
    return r;
}
/* Independent segments from a point-pair table (no cross-connections), mapped
 * into r and kept fully inside it. */
static inline void KshimStarSegs(lv_event_t *e,uint32_t color,lv_opa_t opa,kshim_design_rect_t r,
    int32_t inset,const kshim_star_pt_t *p,int32_t n,int32_t width) {
    int32_t i;
    if(n<2) return;
    r=KshimStarInset(r,inset+width);
    if(r.w<1 || r.h<1) return;
    for(i=0;i+1<n;i+=2)
        KshimSymbolLine(e,color,opa,KshimStarMapX(r,p[i].x),KshimStarMapY(r,p[i].y),
            KshimStarMapX(r,p[i+1].x),KshimStarMapY(r,p[i+1].y),width,true);
}
/* Normalized 0..1000 polylines mapped into a rect, whole artwork kept inside it. */
static inline void KshimStarPoly(lv_event_t *e,uint32_t color,lv_opa_t opa,kshim_design_rect_t r,
    int32_t inset,const kshim_star_pt_t *p,int32_t n,int32_t width,bool closed) {
    int32_t i;
    if(n<2) return;
    r=KshimStarInset(r,inset+width);
    if(r.w<1 || r.h<1) return;
    for(i=0;i<n-1;i++)
        KshimSymbolLine(e,color,opa,KshimStarMapX(r,p[i].x),KshimStarMapY(r,p[i].y),
            KshimStarMapX(r,p[i+1].x),KshimStarMapY(r,p[i+1].y),width,true);
    if(closed)
        KshimSymbolLine(e,color,opa,KshimStarMapX(r,p[n-1].x),KshimStarMapY(r,p[n-1].y),
            KshimStarMapX(r,p[0].x),KshimStarMapY(r,p[0].y),width,true);
}
static inline void KshimStarCircle(lv_event_t *e,uint32_t color,lv_opa_t opa,
    int32_t cx,int32_t cy,int32_t rad,int32_t width) {
    if(rad<1) return;
    KshimSymbolRect(e,color,opa,cx-rad,cy-rad,2*rad,2*rad,rad,width);
}
static inline void KshimStarArc(lv_event_t *e,uint32_t color,lv_opa_t opa,int32_t cx,int32_t cy,
    int32_t rad,const kshim_star_pt_t *d,int32_t n,int32_t start,int32_t count,int32_t width) {
    int32_t i;
    if(rad<1 || count<2) return;
    for(i=0;i<count;i++) {
        const kshim_star_pt_t *a=&d[(start+i)%n], *b=&d[(start+i+1)%n];
        KshimSymbolLine(e,color,opa,cx+a->x*rad/1000,cy+a->y*rad/1000,
            cx+b->x*rad/1000,cy+b->y*rad/1000,width,true);
    }
}
static inline void KshimStarArrow(lv_event_t *e,uint32_t color,lv_opa_t opa,
    int32_t tipx,int32_t tipy,int32_t dx,int32_t dy,int32_t size,int32_t width) {
    int32_t bx=tipx-dx*size/1000, by=tipy-dy*size/1000;
    KshimSymbolLine(e,color,opa,tipx,tipy,bx-dy*size/1000,by+dx*size/1000,width,true);
    KshimSymbolLine(e,color,opa,tipx,tipy,bx+dy*size/1000,by-dx*size/1000,width,true);
}
static inline void KshimStarTip(lv_event_t *e,uint32_t color,lv_opa_t opa,
    int32_t cx,int32_t cy,int32_t rad,int32_t width) {
    static const kshim_star_pt_t p[8]={{0,-100},{26,-26},{100,0},{26,26},{0,100},{-26,26},{-100,0},{-26,-26}};
    int32_t i;
    if(rad<2) return;
    for(i=0;i<8;i++)
        KshimSymbolLine(e,color,opa,cx+p[i].x*rad/100,cy+p[i].y*rad/100,
            cx+p[(i+1)&7].x*rad/100,cy+p[(i+1)&7].y*rad/100,width,true);
}
/* Asymmetric feather: diagonal shaft from lower-left to upper-right, notched
 * contour with several barbs and one uneven duplicate partial edge. */
static inline void KshimStarFeather(lv_event_t *e,uint32_t outline,uint32_t vein,lv_opa_t opa,
    kshim_design_rect_t r,int32_t width) {
    static const kshim_star_pt_t body[16]={
        {820,25},{780,210},{710,350},{650,430},{790,375},{695,550},{600,620},{720,575},
        {570,770},{395,840},{260,815},{140,935},{195,640},{270,440},{390,280},{620,155}
    };
    static const kshim_star_pt_t barbs[10]={
        {240,850},{150,900}, {350,745},{260,808}, {455,645},{395,700},
        {700,320},{790,375}, {720,420},{655,470}
    };
    static const kshim_star_pt_t dup[4]={{795,55},{757,215},{690,352},{632,432}};
    if(r.w<2 || r.h<2) return;
    KshimStarPoly(e,outline,opa,r,0,body,16,width,true);
    KshimSymbolLine(e,vein,opa,KshimStarMapX(r,100),KshimStarMapY(r,980),
        KshimStarMapX(r,780),KshimStarMapY(r,110),width,true);
    KshimStarSegs(e,vein,opa,r,0,barbs,10,1);
    KshimStarSegs(e,outline,opa,r,0,dup,4,1);
}
/* Quiet baseline plus a genuine mountain cluster: open ridge polyline and a
 * few interior snow/ridge strokes, all mapped inside a small local rect. */
static inline void KshimStarRange(lv_event_t *e,uint32_t color,lv_opa_t opa,
    kshim_design_rect_t r,int32_t width,const kshim_star_pt_t *p,int32_t n,
    const kshim_star_pt_t *snow,int32_t sn) {
    if(r.w<2 || r.h<2 || n<2) return;
    KshimSymbolLine(e,color,opa,KshimStarMapX(r,0),KshimStarMapY(r,930),
        KshimStarMapX(r,1000),KshimStarMapY(r,930),width,true);
    KshimStarPoly(e,color,opa,r,0,p,n,width,false);
    KshimStarSegs(e,color,opa,r,0,snow,sn,1);
}
static const kshim_star_pt_t kKshimStarRing[12]={
    {1000,0},{866,500},{500,866},{0,1000},{-500,866},{-866,500},
    {-1000,0},{-866,-500},{-500,-866},{0,-1000},{500,-866},{866,-500}
};
static inline void KshimStarIcon(lv_event_t *e,kshim_design_rect_t r,uint32_t role,
    uint32_t ink,uint32_t accent,lv_opa_t opa,int32_t stroke) {
    kshim_design_rect_t q;
    int32_t cx,cy,rad;
    r=KshimStarInset(r,stroke);
    if(r.w<3 || r.h<3) return;
    cx=r.x+r.w/2; cy=r.y+r.h/2;
    switch(role) {
    case KSHIM_MENU_ROLE_ANDROID:
        q=KshimStarSub(r,200,320,600,360);
        KshimSymbolRect(e,ink,opa,q.x,q.y,q.w,q.h,q.h/3,stroke);
        KshimSymbolLine(e,ink,opa,q.x+q.w/5,q.y,q.x+q.w/8,q.y-q.h*2/5,stroke,true);
        KshimSymbolLine(e,ink,opa,q.x+q.w*4/5,q.y,q.x+q.w*7/8,q.y-q.h*2/5,stroke,true);
        KshimSymbolRect(e,accent,opa,q.x+q.w/5,q.y+q.h*2/5,q.w/7,q.h/6,q.w/14,0);
        KshimSymbolRect(e,accent,opa,q.x+q.w*4/5-q.w/7,q.y+q.h*2/5,q.w/7,q.h/6,q.w/14,0);
        q=KshimStarSub(r,300,760,400,180);
        KshimSymbolRect(e,ink,opa,q.x,q.y,q.w,q.h,q.h/2,stroke);
        break;
    case KSHIM_MENU_ROLE_INTERNAL:
    case KSHIM_MENU_ROLE_BASE:
    case KSHIM_MENU_ROLE_DEFAULT:
        q=KshimStarSub(r,120,240,760,520);
        KshimSymbolRect(e,ink,opa,q.x,q.y,q.w,q.h,q.h/4,stroke);
        KshimSymbolLine(e,ink,opa,q.x+q.w/8,q.y+q.h*58/100,q.x+q.w*7/8,q.y+q.h*58/100,stroke,true);
        KshimSymbolRect(e,accent,opa,q.x+q.w/8,q.y+q.h*72/100,q.w/9,q.h/8,q.w/18,0);
        break;
    case KSHIM_MENU_ROLE_TF: {
        static const kshim_star_pt_t card[5]={{150,180},{660,180},{860,380},{860,820},{150,820}};
        static const kshim_star_pt_t fold[3]={{660,180},{660,380},{860,380}};
        KshimStarPoly(e,ink,opa,r,0,card,5,stroke,true);
        KshimStarPoly(e,ink,opa,r,0,fold,3,stroke,false);
        KshimSymbolLine(e,ink,opa,KshimStarMapX(r,260),KshimStarMapY(r,470),KshimStarMapX(r,640),KshimStarMapY(r,470),stroke,true);
        KshimSymbolLine(e,ink,opa,KshimStarMapX(r,260),KshimStarMapY(r,620),KshimStarMapX(r,640),KshimStarMapY(r,620),stroke,true);
        break;
    }
    case KSHIM_MENU_ROLE_ADVANCED:
    case KSHIM_MENU_ROLE_SETTINGS: {
        static const int32_t tooth[6]={0,2,4,6,8,10};
        int32_t i, ri=KshimStarMapR(r,240), ro=KshimStarMapR(r,440);
        for(i=0;i<6;i++) {
            const kshim_star_pt_t *d=&kKshimStarRing[tooth[i]];
            KshimSymbolLine(e,ink,opa,cx+d->x*ri/1000,cy+d->y*ri/1000,
                cx+d->x*ro/1000,cy+d->y*ro/1000,stroke,true);
        }
        KshimStarCircle(e,ink,opa,cx,cy,ri,stroke);
        break;
    }
    case KSHIM_MENU_ROLE_LANGUAGE:
    case KSHIM_MENU_ROLE_LANG_ZH:
    case KSHIM_MENU_ROLE_LANG_EN:
        rad=KshimStarMapR(r,420);
        KshimStarCircle(e,ink,opa,cx,cy,rad,stroke);
        KshimSymbolLine(e,ink,opa,cx-rad,cy,cx+rad,cy,stroke,true);
        KshimSymbolLine(e,ink,opa,cx,cy-rad,cx,cy+rad,stroke,true);
        break;
    case KSHIM_MENU_ROLE_RESTART:
        rad=KshimStarMapR(r,400);
        KshimStarArc(e,ink,opa,cx,cy,rad,kKshimStarRing,12,1,9,stroke);
        KshimStarArrow(e,ink,opa,cx+kKshimStarRing[10].x*rad/1000,cy+kKshimStarRing[10].y*rad/1000,
            866,500,KshimStarMapR(r,340),stroke);
        break;
    case KSHIM_MENU_ROLE_RECOVERY:
        rad=KshimStarMapR(r,400);
        KshimStarArc(e,ink,opa,cx,cy,rad,kKshimStarRing,12,0,9,stroke);
        KshimStarArrow(e,ink,opa,cx,cy-rad,1000,0,KshimStarMapR(r,340),stroke);
        break;
    case KSHIM_MENU_ROLE_OFF:
        rad=KshimStarMapR(r,400);
        KshimStarArc(e,ink,opa,cx,cy,rad,kKshimStarRing,12,10,10,stroke);
        KshimSymbolLine(e,ink,opa,cx,cy-rad*12/10,cx,cy-rad/3,stroke,true);
        break;
    case KSHIM_MENU_ROLE_DIAGNOSTIC:
        KshimSymbolLine(e,ink,opa,KshimStarMapX(r,160),KshimStarMapY(r,860),
            KshimStarMapX(r,560),KshimStarMapY(r,460),stroke,true);
        KshimStarCircle(e,ink,opa,KshimStarMapX(r,690),KshimStarMapY(r,330),KshimStarMapR(r,240),stroke);
        KshimSymbolLine(e,ink,opa,KshimStarMapX(r,770),KshimStarMapY(r,150),
            KshimStarMapX(r,940),KshimStarMapY(r,320),stroke,true);
        break;
    case KSHIM_MENU_ROLE_BACK:
        KshimSymbolLine(e,ink,opa,KshimStarMapX(r,880),KshimStarMapY(r,500),
            KshimStarMapX(r,180),KshimStarMapY(r,500),stroke,true);
        KshimSymbolLine(e,ink,opa,KshimStarMapX(r,180),KshimStarMapY(r,500),
            KshimStarMapX(r,440),KshimStarMapY(r,240),stroke,true);
        KshimSymbolLine(e,ink,opa,KshimStarMapX(r,180),KshimStarMapY(r,500),
            KshimStarMapX(r,440),KshimStarMapY(r,760),stroke,true);
        break;
    case KSHIM_MENU_ROLE_FASTBOOT: {
        static const kshim_star_pt_t bolt[6]={{620,60},{280,540},{480,540},{380,940},{760,430},{540,430}};
        KshimStarPoly(e,ink,opa,r,0,bolt,6,stroke,true);
        break;
    }
    case KSHIM_MENU_ROLE_EDL:
        q=KshimStarSub(r,280,300,440,400);
        KshimSymbolRect(e,ink,opa,q.x,q.y,q.w,q.h,q.h/4,stroke);
        KshimSymbolLine(e,ink,opa,KshimStarMapX(r,400),KshimStarMapY(r,300),
            KshimStarMapX(r,400),KshimStarMapY(r,120),stroke,true);
        KshimSymbolLine(e,ink,opa,KshimStarMapX(r,600),KshimStarMapY(r,300),
            KshimStarMapX(r,600),KshimStarMapY(r,120),stroke,true);
        KshimSymbolLine(e,ink,opa,KshimStarMapX(r,500),KshimStarMapY(r,700),
            KshimStarMapX(r,500),KshimStarMapY(r,880),stroke,true);
        break;
    case KSHIM_MENU_ROLE_DEVICE_INFO:
    case KSHIM_MENU_ROLE_DEVICE:
    case KSHIM_MENU_ROLE_DEVICE_AUTO:
    case KSHIM_MENU_ROLE_DEVICE_TB322FC:
    case KSHIM_MENU_ROLE_DEVICE_TB323FU:
        /* Simple info tablet: an outlined rounded frame with a dot-and-stem i,
         * drawn from the existing outline primitives only. */
        q=KshimStarSub(r,220,150,560,700);
        KshimSymbolRect(e,ink,opa,q.x,q.y,q.w,q.h,q.h/8,stroke);
        KshimSymbolRect(e,accent,opa,q.x+q.w/2-q.w/16,q.y+q.h/7,q.w/8,q.h/10,q.w/16,0);
        KshimSymbolLine(e,ink,opa,q.x+q.w/2,q.y+q.h*2/5,q.x+q.w/2,q.y+q.h*5/6,stroke,true);
        break;
    default:
        q=KshimStarSub(r,180,140,640,720);
        KshimSymbolRect(e,ink,opa,q.x,q.y,q.w,q.h,q.h/6,stroke);
        KshimSymbolLine(e,ink,opa,q.x+q.w/8,q.y+q.h*22/100,q.x+q.w*7/8,q.y+q.h*22/100,stroke,true);
        KshimSymbolRect(e,accent,opa,q.x+q.w*42/100,q.y+q.h*82/100,q.w/8,q.h/10,3,0);
        break;
    }
}
static inline bool KshimStarActive(void) {
    return mMenuDesign.kind==KSHIM_DESIGN_STAR_CHART && !mDesignGeometry.compact;
}
static inline int32_t KshimStarStroke(void) {
    int32_t s=KshimDesignStroke(2); return s<1?1:s;
}

static inline void KshimStarPanelDraw(lv_event_t *event) {
    lv_obj_t *panel_obj;
    lv_area_t a;
    kshim_design_rect_t panel,box,title,art,footer,credit,detail,fr;
    static const kshim_star_pt_t tablet[4]={{200,50},{900,180},{780,950},{80,820}};
    static const kshim_star_pt_t inner[4]={{235,120},{830,230},{715,880},{155,770}};
    /* 32 fixed oval vertices, gently tilted up to the right. */
    static const kshim_star_pt_t orbit[32]={
        {992,413},{995,482},{978,552},{943,620},{891,683},{824,739},{745,786},{656,822},
        {561,845},{463,855},{367,852},{277,835},{195,805},{124,764},{68,712},{29,653},
        {7,587},{5,518},{22,448},{57,380},{109,317},{176,261},{255,214},{344,178},
        {439,155},{537,145},{633,148},{723,165},{805,195},{876,236},{932,288},{971,347}
    };
    /* Short offset second arc for the hand-sketch feel. */
    static const kshim_star_pt_t orbit2[7]={
        {992,542},{957,610},{905,673},{838,729},{759,776},{670,812},{575,835}
    };
    static const kshim_star_pt_t ridgeL[9]={{0,880},{90,760},{200,470},{310,720},{430,380},
        {560,690},{680,540},{820,780},{1000,880}};
    static const kshim_star_pt_t snowL[6]={{200,500},{250,540},{430,410},{480,455},{680,570},{725,610}};
    static const kshim_star_pt_t ridgeR[9]={{0,860},{110,720},{230,440},{350,700},{470,560},
        {610,780},{740,470},{870,740},{1000,860}};
    static const kshim_star_pt_t snowR[4]={{230,470},{280,515},{740,500},{785,545}};
    uint32_t ink,gold,muted;
    int32_t ins,stroke,fw,avail,bottom,top,x0;
    lv_opa_t opa=LV_OPA_COVER;
    if(!KshimStarActive()) return;
    panel_obj=lv_event_get_target_obj(event);
    if(!panel_obj) return;
    lv_obj_get_coords(panel_obj,&a);
    if(a.x2<a.x1 || a.y2<a.y1) return;
    ink=mMenuStyle.Text; gold=mMenuStyle.FocusBorder; muted=mMenuStyle.Muted;
    ins=KshimDesignPx(6); stroke=KshimStarStroke();
    panel.x=a.x1; panel.y=a.y1; panel.w=a.x2-a.x1+1; panel.h=a.y2-a.y1+1;
    title=KshimStarShift(mDesignGeometry.title,panel.x,panel.y);
    art=KshimStarShift(mDesignGeometry.illustration,panel.x,panel.y);
    footer=KshimStarShift(mDesignGeometry.footer,panel.x,panel.y);
    credit=KshimStarShift(mDesignGeometry.credit,panel.x,panel.y);
    detail=KshimStarShift(mDesignGeometry.detail,panel.x,panel.y);
    box=KshimStarInset(panel,ins);
    if(box.w<1 || box.h<1) return;

    /* Feather in the reserved space immediately left of the title. */
    avail=title.x-box.x-KshimDesignPx(4);
    fw=KshimDesignPx(50);
    if(fw>avail) fw=avail;
    if(fw>=KshimDesignPx(14)) {
        fr.x=title.x-fw-KshimDesignPx(4);
        fr.y=title.y-KshimDesignPx(2);
        fr.w=fw;
        fr.h=(title.h>0?title.h:KshimDesignPx(18))+KshimDesignPx(4);
        if(fr.y<box.y) fr.y=box.y;
        if(fr.y+fr.h>box.y+box.h) fr.h=box.y+box.h-fr.y;
        if(fr.h>=KshimDesignPx(14) && fr.x>=box.x && fr.x+fr.w<=title.x)
            KshimStarFeather(event,gold,ink,opa,fr,stroke);
    }
    /* Two uneven short gold underlines just below the title, constrained by the list top. */
    top=title.y+(title.h>0?title.h:KshimDesignPx(18))+KshimDesignPx(3);
    bottom=box.y+box.h;
    if(mDesignGeometry.list.h>0 && panel.y+mDesignGeometry.list.y-KshimDesignPx(2)<bottom)
        bottom=panel.y+mDesignGeometry.list.y-KshimDesignPx(2);
    x0=title.x<box.x?box.x:title.x;
    if(top+KshimDesignPx(4)+stroke<bottom && box.x+box.w-x0>KshimDesignPx(60)) {
        int32_t room=box.x+box.w-x0, l1, l2, ox=KshimDesignPx(10);
        l1=KshimDesignPx(150); if(l1>room) l1=room;
        if(title.w>0 && l1>title.w) l1=title.w;
        l2=KshimDesignPx(104); if(l2>l1-KshimDesignPx(6)) l2=l1-KshimDesignPx(6);
        if(l2<1) l2=1;
        if(ox+l2>room) ox=room-l2;
        if(ox<0) ox=0;
        KshimSymbolLine(event,gold,opa,x0,top,x0+l1,top,stroke+1,true);
        KshimSymbolLine(event,gold,opa,x0+ox,top+KshimDesignPx(4),
            x0+ox+l2,top+KshimDesignPx(4),stroke,true);
    }
    /* Illustration: a fixed 12:5 canvas centred inside the art rect (letterboxed,
     * never stretched), holding a tilted tablet on its own box, miniature
     * feather, orbiting oval, stars and moon. */
    if(art.w>KshimDesignPx(40) && art.h>KshimDesignPx(40)) {
        kshim_design_rect_t cvs,tb,fea;
        art=KshimStarInset(art,ins);
        cvs=KshimStarAspect(art,12,5);
        tb.h=cvs.h*75/100;
        tb.w=tb.h*82/100;
        if(tb.w<1) tb.w=1;
        if(tb.h<1) tb.h=1;
        tb.x=cvs.x+cvs.w/2-tb.w/2;
        tb.y=cvs.y+cvs.h*52/100-tb.h/2;
        KshimStarPoly(event,muted,opa,tb,0,tablet,4,stroke,true);
        KshimStarPoly(event,muted,opa,tb,0,inner,4,stroke,true);
        KshimStarPoly(event,ink,opa,cvs,0,orbit,32,stroke,true);
        KshimStarPoly(event,muted,opa,cvs,0,orbit2,7,1,false);
        /* Feather subrect inside the tablet keeps the feather's own aspect. */
        fea=KshimStarAspect(KshimStarSub(tb,300,180,400,640),680,955);
        KshimStarFeather(event,gold,ink,opa,fea,stroke);
        KshimStarTip(event,ink,opa,KshimStarMapX(cvs,110),KshimStarMapY(cvs,180),KshimStarMapR(cvs,45),stroke);
        KshimStarTip(event,gold,opa,KshimStarMapX(cvs,750),KshimStarMapY(cvs,140),KshimStarMapR(cvs,32),stroke);
        KshimStarTip(event,muted,opa,KshimStarMapX(cvs,120),KshimStarMapY(cvs,820),KshimStarMapR(cvs,38),stroke);
        KshimStarTip(event,gold,opa,KshimStarMapX(cvs,900),KshimStarMapY(cvs,760),KshimStarMapR(cvs,40),stroke);
        {
            int32_t mx=KshimStarMapX(cvs,850), my=KshimStarMapY(cvs,330), mr=KshimStarMapR(cvs,70);
            KshimStarCircle(event,muted,opa,mx,my,mr,stroke);
            KshimSymbolLine(event,muted,opa,mx-mr/3,my-mr/4,mx-mr/8,my-mr/3,stroke,true);
            KshimSymbolLine(event,muted,opa,mx+mr/5,my+mr/6,mx+mr/2,my+mr/12,stroke,true);
            KshimSymbolLine(event,muted,opa,mx-mr/2,my+mr/3,mx-mr/4,my+mr/2,stroke,true);
        }
    }
    /* Two quiet local mountain clusters in the band above the footer: a thin
     * baseline plus real peaks, kept clear of the list's last row, the boot
     * button and any detail text. */
    {
        int32_t lbot,bbot,cbot,ctop,cw,ch,availh;
        kshim_design_rect_t lcl,rcl;
        lbot=panel.y+mDesignGeometry.list.y+mDesignGeometry.list.h;
        bbot=panel.y+mDesignGeometry.boot.y+mDesignGeometry.boot.h;
        cbot=box.y+box.h;
        ctop=box.y;
        if(footer.h>0 && footer.y<cbot) cbot=footer.y;
        if(credit.h>0 && credit.y<cbot) cbot=credit.y;
        if(art.h>0 && art.y+art.h>ctop) ctop=art.y+art.h;
        if(detail.h>0 && detail.y+detail.h>ctop) ctop=detail.y+detail.h;
        if(mDesignGeometry.list.h>0 && lbot>ctop) ctop=lbot;
        if(mDesignGeometry.boot.h>0 && bbot>ctop) ctop=bbot;
        ctop+=KshimDesignPx(2);
        cbot-=KshimDesignPx(3);
        availh=cbot-ctop;
        ch=KshimDesignPx(52); if(ch>availh) ch=availh;
        if(ch>KshimDesignPx(60)) ch=KshimDesignPx(60);
        cw=KshimDesignPx(180);
        if(cw>box.w/3) cw=box.w/3;
        if(ch>=KshimDesignPx(14) && cw>=KshimDesignPx(40)) {
            int32_t pad=KshimDesignPx(8);
            lcl.x=box.x+pad; lcl.y=cbot-ch; lcl.w=cw; lcl.h=ch;
            rcl.x=box.x+box.w-pad-cw; rcl.y=cbot-ch; rcl.w=cw; rcl.h=ch;
            KshimStarRange(event,muted,opa,lcl,stroke,ridgeL,9,snowL,6);
            KshimStarRange(event,muted,opa,rcl,stroke,ridgeR,9,snowR,4);
        }
    }
}

static inline void KshimStarRowDraw(lv_event_t *event,size_t index) {
    lv_obj_t *button;
    lv_area_t a;
    kshim_design_item_geometry_t metrics;
    kshim_design_rect_t ir;
    uint32_t role,gold;
    int32_t stroke,thin,w,h,cx,cy,ch;
    bool selected;
    lv_opa_t opa=LV_OPA_COVER;
    if(!KshimStarActive() || !mContext) return;
    if(index>=(size_t)mDesignGeometry.count) return;
    button=lv_event_get_target_obj(event);
    if(!button) return;
    lv_obj_get_coords(button,&a);
    if(a.x2<a.x1 || a.y2<a.y1) return;
    kshim_design_item_layout(&mMenuDesign,&mDesignGeometry,(unsigned)index,
        lv_font_get_line_height(lv_obj_get_style_text_font(button,LV_PART_MAIN)),&metrics);
    selected=lv_obj_has_state(button,LV_STATE_FOCUSED);
    gold=mMenuStyle.FocusBorder; stroke=KshimStarStroke(); thin=stroke<2?1:stroke;
    w=a.x2-a.x1+1; h=a.y2-a.y1+1;
    if(selected && w>8 && h>8) {
        int32_t radius=KshimDesignPx(mMenuDesign.row_radius);
        int32_t ro=radius-1; if(ro<0) ro=0;
        int32_t ri=radius-3; if(ri<0) ri=0;
        KshimSymbolRect(event,gold,opa,a.x1+1,a.y1+1,w-2,h-2,ro,thin);
        KshimSymbolRect(event,gold,opa,a.x1+3,a.y1+3,w-6,h-6,ri,thin);
    }
    role=mContext->Roles[index];
    if(metrics.icon.w>2 && metrics.icon.h>2) {
        ir.x=a.x1+metrics.icon.x; ir.y=a.y1+metrics.icon.y;
        ir.w=metrics.icon.w; ir.h=metrics.icon.h;
        KshimStarIcon(event,ir,role,mMenuStyle.Text,gold,opa,stroke);
    }
    if(role==KSHIM_MENU_ROLE_ADVANCED || role==KSHIM_MENU_ROLE_LANGUAGE ||
        role==KSHIM_MENU_ROLE_SETTINGS || role==KSHIM_MENU_ROLE_DEVICE || role==KSHIM_MENU_ROLE_DEFAULT ||
        (role==KSHIM_MENU_ROLE_DEVICE_INFO && mContext->ActionLabels[index]!=NULL && strcmp(mContext->ActionLabels[index], KSHIM_MENU_ACTION_ENTER)==0)) {
        cx=a.x2-KshimDesignPx(10); cy=(a.y1+a.y2)/2; ch=KshimDesignPx(5);
        KshimSymbolLine(event,gold,opa,cx-ch,cy-ch,cx,cy,thin,true);
        KshimSymbolLine(event,gold,opa,cx,cy,cx-ch,cy+ch,thin,true);
    }
}

static inline void KshimStarBootDraw(lv_event_t *event) {
    lv_obj_t *button;
    lv_area_t a;
    uint32_t gold;
    int32_t stroke,thin,w,h;
    lv_opa_t opa=LV_OPA_COVER;
    if(!KshimStarActive()) return;
    button=lv_event_get_target_obj(event);
    if(!button) return;
    lv_obj_get_coords(button,&a);
    w=a.x2-a.x1+1; h=a.y2-a.y1+1;
    if(w<8 || h<8) return;
    gold=mMenuStyle.FocusBorder; stroke=KshimStarStroke(); thin=stroke<2?1:stroke;
    int32_t radius=(mMenuDesign.boot_radius==255?h/2:KshimDesignPx(mMenuDesign.boot_radius));
    int32_t ro=radius-1; if(ro<0) ro=0;
    int32_t ri=radius-3; if(ri<0) ri=0;
    KshimSymbolRect(event,gold,opa,a.x1+1,a.y1+1,w-2,h-2,ro,thin);
    KshimSymbolRect(event,gold,opa,a.x1+3,a.y1+3,w-6,h-6,ri,thin);
}
#endif
