#include "../active-loop.c"
#include <assert.h>
static unsigned char F[ERP_LEN];
static void run(void){ on_erp(AZ_BUS_ERP_IN, F, ERP_LEN, NULL); }
int main(void){
    prewarm_filter();
    assert(FS.snap==0 && FS.prev==0 && FS.del==0);
    FS.cfg = CFG_ENABLE|CFG_HOTCUE|CFG_CLEAR;
    memset(F,0,sizeof F); run();                       /* baseline */
    F[A_SH_O]|=1u<<A_SH_B; run();                       /* shift */
    F[A_MEM_O]|=1u<<A_MEM_B; run();                     /* +memory -> SET A, PRESS A, eaten */
    assert(!(F[A_MEM_O]&(1u<<A_MEM_B)));
    memset(F,0,sizeof F); run();
    F[B_SH_O]|=1u<<B_SH_B; F[B_PAD_O]=4; run();         /* shift+pad3 B */
    assert(F[B_PAD_O]==0);
    memset(F,0,sizeof F); run();
    F[A_SEC_O]|=1u<<A_SEC_B; run(); memset(F,0,sizeof F); run();
    F[A_PAD_O]=1; run(); assert(F[A_PAD_O]==1);          /* plain pad passes, PRESS A */
    uint64_t s=FS.snap;
    #define N(p) (unsigned)((s>>(p))&15)
    printf("A set=%u clr=%u pad=%u press=%u sec=%u | B pad=%u kind=%u press=%u | hb=%u\n",
      N(0),N(4),N(8),N(12),N(16),N(28+8),N(28+24),N(28+12),(unsigned)(s>>56));
    { uint64_t t0=now_ns(), worst=0; for(int i=0;i<200000;i++){ uint64_t a=now_ns(); F[A_PAD_O]=(unsigned char)(i&1); F[A_SH_O]=(unsigned char)((i&2)<<6); run(); uint64_t d=now_ns()-a; if(i>10&&d>worst) worst=d; } printf("avg %.0f ns/call, worst %llu ns (incl. 2 clock reads)\n",(now_ns()-t0)/200000.0,(unsigned long long)worst); }
    return !(N(0)==1&&N(4)==0&&N(8)==0&&N(12)==2&&N(16)==1&&N(36)==1&&N(52)==3&&N(40)==1&&(s>>56)==9);
}
