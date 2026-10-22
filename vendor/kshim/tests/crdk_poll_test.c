/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <Library/cr_debug_uart_poll.h>
#include <Library/cr_splash.h>
#include <Library/cr_resources.h>
#define BASE 0x100000U
struct model { uint32_t reg[0x4000/4]; uint64_t ticks; unsigned reads,writes,txbytes; bool stall,abort_stall,unstable; };
static uint32_t read32(void *v, uintptr_t a)
{
    struct model *m=v; assert(a>=BASE && a-BASE<sizeof(m->reg) && !(a&3));
    ++m->reads; unsigned off=a-BASE;
    if (off==0x780) { m->reg[0x804/4]=0; return 0x44332211; }
    if (m->unstable && off==0x1014) return ++m->reg[off/4];
    return m->reg[off/4];
}
static void write32(void *v, uintptr_t a, uint32_t value)
{
    struct model *m=v; assert(a>=BASE && a-BASE<sizeof(m->reg) && !(a&3));
    ++m->writes; unsigned off=a-BASE;
    if(off==0x618 || off==0x648) { m->reg[(off-8)/4]&=~value; return; }
    m->reg[off/4]=value;
    if(off==0x600) { m->reg[0x40/4]|=1; m->txbytes=0; }
    if(off==0x630) m->reg[0x40/4]|=1U<<12;
    if(off==0x700) {
        m->txbytes+=4;
        if(!m->stall && m->txbytes>=m->reg[0x270/4]) {m->reg[0x610/4]=1;m->reg[0x40/4]&=~1U;}
    }
    if((off==0x604 || off==0x634) && !m->abort_stall) {
        m->reg[(off+12)/4]=1U<<5; m->reg[0x40/4]&=~(off==0x604 ? 1U : 1U<<12);
    }
}
static uint64_t now(void *v) { return ((struct model*)v)->ticks; }
static void delay(void *v,uint32_t us) { ((struct model*)v)->ticks+=us; }
static struct CrIo io(struct model *m) { return (struct CrIo){m,read32,write32,now,delay}; }
static void uart(void)
{
    struct model m={0}; struct CrDebugUartPoll u={.io=io(&m),.base=BASE,.size=0x1000,.timeout_us=20};
    m.reg[0x68/4]=2U<<8; m.reg[0xe24/4]=m.reg[0xe28/4]=16U<<16;
    m.reg[0x260/4]=0xdead; m.reg[0xe1c/4]=0xf; m.reg[0x630/4]=(1U<<27)|3;
    m.reg[0x40/4]=1U<<12; /* Inherited polling RX must be restored. */
    u.size=0x100; assert(CrDebugUartPollInit(&u)==CR_BUS_INVALID); assert(!m.reads);
    u.size=0x1000; m.reg[0x64/4]=1; assert(CrDebugUartPollInit(&u)==CR_BUS_UNSUPPORTED); assert(!m.writes);
    m.reg[0x64/4]=0; assert(!CrDebugUartPollInit(&u));
    assert(m.reg[0x260/4]==0x7f8fe && m.reg[0x264/4]==0xffefe);
    uint8_t b=0; assert(!CrDebugUartPollRead(&u,&b));
    m.reg[0x804/4]=(1U<<31)|(3U<<28)|1;
    assert(CrDebugUartPollRead(&u,&b)==1 && b==0x11);
    assert(CrDebugUartPollRead(&u,&b)==1 && b==0x22);
    assert(CrDebugUartPollRead(&u,&b)==1 && b==0x33);
    assert(!CrDebugUartPollRead(&u,&b));
    m.reg[0x640/4]=1U<<9; m.reg[0x804/4]=1;
    assert(CrDebugUartPollRead(&u,&b)==CR_BUS_IO); assert(!u.poisoned);
    assert(!CrDebugUartPollRead(&u,&b));
    assert(!CrDebugUartPollWrite(&u,"abcdef",6)); assert(m.txbytes==8);
    m.stall=true; assert(CrDebugUartPollWrite(&u,"x",1)==CR_BUS_TIMEOUT); assert(!u.poisoned);
    m.stall=false; assert(!CrDebugUartPollWrite(&u,"y",1));
    m.stall=m.abort_stall=true; assert(CrDebugUartPollWrite(&u,"z",1)==CR_BUS_TIMEOUT); assert(u.poisoned);
    unsigned before=m.writes; assert(CrDebugUartPollWrite(&u,"z",1)==CR_BUS_BUSY); assert(m.writes==before);
    assert(CrDebugUartPollQuiesce(&u)==CR_BUS_TIMEOUT); assert(u.claimed);
    assert(m.reg[0x260/4]==0x7f8fe); /* Failed abort cannot release configuration. */
    m.abort_stall=false; assert(!CrDebugUartPollQuiesce(&u));
    assert(!u.claimed && !u.initialized && !u.poisoned);
    assert(m.reg[0x260/4]==0xdead && m.reg[0xe1c/4]==0xf);
    assert(m.reg[0x630/4]==((1U<<27)|3) && (m.reg[0x40/4]&(1U<<12)));
    before=m.writes; assert(!CrDebugUartPollQuiesce(&u)); assert(m.writes==before);
}
static void pipe_setup(struct model *m,unsigned p,unsigned width,unsigned x,uint32_t address)
{
    unsigned b=p/4;
    m->reg[b]=m->reg[b+3]=(48U<<16)|width;
    m->reg[b+2]=x; m->reg[b+5]=address; m->reg[b+9]=64*4;
    m->reg[b+12]=0x237ff; m->reg[b+13]=0x03020001;
}
static void splash(void)
{
    struct model m={0}; struct CrIo ops=io(&m);
    struct CrSplashResources r={.base=BASE,.size=0x4000,.reserved_base=0x80000000,
        .reserved_size=64*48*4,.pipe={0x1000,0x2000},.pipe_id={0,1},.pipe_count=2,
        .ctl={0},.ctl_count=1,.mixer={0x3000,0x3100},.mixer_count=2};
    struct CrSplash fb;
    assert(CrSplashRead(&r,&ops,&fb)==CR_BUS_UNSUPPORTED); assert(!m.writes);
    m.reg[0xf4/4]=1; m.reg[0]=1; m.reg[0x3004/4]=(48U<<16)|64;
    pipe_setup(&m,0x1000,64,0,0x80000000);
    assert(!CrSplashRead(&r,&ops,&fb)); assert(fb.width==64 && fb.height==48 && fb.format==CR_SPLASH_ARGB8888);
    m.reg[0x1034/4]=0x03010002; assert(!CrSplashRead(&r,&ops,&fb)); assert(fb.format==CR_SPLASH_ABGR8888);
    m.reg[0x1034/4]=0x03020001;
    m.reg[0x1030/4]=0x22216; m.reg[0x1034/4]=0x00010002; m.reg[0x1024/4]=128;
    assert(!CrSplashRead(&r,&ops,&fb)); assert(fb.format==CR_SPLASH_RGB565 && fb.stride==128 && fb.size==64*48*2);
    m.reg[0x1030/4]=0x237ff; m.reg[0x1034/4]=0x03020001; m.reg[0x1024/4]=256;
    --r.reserved_size; assert(CrSplashRead(&r,&ops,&fb)==CR_BUS_INVALID); ++r.reserved_size;
    m.reg[0x1030/4]|=1U<<30; assert(CrSplashRead(&r,&ops,&fb)==CR_BUS_UNSUPPORTED); m.reg[0x1030/4]&=~(1U<<30);
    m.reg[0]|=1U<<6; assert(CrSplashRead(&r,&ops,&fb)==CR_BUS_UNSUPPORTED); m.reg[0]=1;
    m.reg[0x1010/4]=1; assert(CrSplashRead(&r,&ops,&fb)==CR_BUS_UNSUPPORTED); m.reg[0x1010/4]=0;
    m.reg[0x3004/4]=(48U<<16)|32; m.reg[0x3104/4]=(48U<<16)|32; m.reg[0x3100/4]=1U<<31;
    m.reg[1]=1U<<3; pipe_setup(&m,0x1000,32,0,0x80000000); pipe_setup(&m,0x2000,32,32,0x80000000);
    assert(!CrSplashRead(&r,&ops,&fb)); assert(fb.width==64 && fb.stride==256);
    m.reg[0x3100/4]=0; assert(CrSplashRead(&r,&ops,&fb)==CR_BUS_UNSUPPORTED); m.reg[0x3100/4]=1U<<31;
    m.unstable=true; assert(CrSplashRead(&r,&ops,&fb)!=0); assert(!m.writes);
}
static void bounded(void)
{
    struct model m={0}; struct CrIo ops=io(&m); uint32_t a;
    assert(CrCmdDbLookup(&ops,BASE,143,"ldoa1",&a)==CR_BUS_INVALID); assert(!m.reads);
    assert(CrCmdDbLookupDictionary(&ops,0,"ldoa1",&a)==CR_BUS_INVALID); assert(!m.reads);
    struct CrQupContext p={0}; assert(CrRpmhPollClaim(&p)==CR_BUS_INVALID);
    uint8_t slot;
    assert(CrClockPollPrepare(&p,&slot)==CR_BUS_INVALID);
    assert(CrClockPollRestore(&p)==CR_BUS_INVALID);
    struct CrGeni g={0}; struct CrGeniConfig c={.base=BASE,.size=0xe00,.protocol=CR_GENI_I2C,.source_hz=19200000,.speed_hz=400000};
    assert(CrGeniInit(&g,&c,&ops)==CR_BUS_INVALID); assert(!m.reads);
}
int main(void) { uart(); splash(); bounded(); puts("CrDK polling, splash and bounds passed"); }
