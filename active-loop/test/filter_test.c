/* 0.2.8 frame filter: SHIFT+MEMORY/DELETE (as before), SHIFT+pad arm (pad eaten + counted),
 * DELETE held = eaten, DELETE+pad = SHIFT+pad, DELETE tap replay, SHIFT+pad replay request. */
#define _GNU_SOURCE
#include <time.h>
static unsigned long nclk, nsteps;              /* every clock read the filter makes is counted (al028c: 2 per call, entry + exit) */
#define clock_gettime(c, t) (nclk++, clock_gettime((c), (t)))
#include "../active-loop.c"
#include <assert.h>
#include <unistd.h>
static unsigned char F[ERP_LEN];
/* input state per side; step() builds a fresh frame from it (the filter edits F in place) */
static int ish[2], imm[2], idl[2], ipad[2];
static void step(void){
    memset(F,0,sizeof F);
    for (int s=0;s<2;s++) {
        if (ish[s]) F[SO[s].sh_o]  |= (unsigned char)(1u << SO[s].sh_b);
        if (imm[s]) F[SO[s].mem_o] |= (unsigned char)(1u << SO[s].mem_b);
        if (idl[s]) F[SO[s].del_o] |= (unsigned char)(1u << SO[s].del_b);
        F[SO[s].pad_o] |= (unsigned char)ipad[s];
    }
    nsteps++;
    on_erp(AZ_BUS_ERP_IN, F, ERP_LEN, NULL);
}
static void idle(void){ memset(ish,0,sizeof ish); memset(imm,0,sizeof imm); memset(idl,0,sizeof idl); memset(ipad,0,sizeof ipad); step(); }
#define HAS_SH(s)  ((F[SO[s].sh_o]  >> SO[s].sh_b)  & 1u)
#define HAS_DL(s)  ((F[SO[s].del_o] >> SO[s].del_b) & 1u)
#define HAS_MM(s)  ((F[SO[s].mem_o] >> SO[s].mem_b) & 1u)
#define PAD(s)     (F[SO[s].pad_o])
static unsigned nibv(int s, int pos){ return (unsigned)((FS.snap >> (28*s + pos)) & 15u); }
static void expire(void){
    FS.rp_frames = 0; step(); FS.rp_frames = 40;
    /* 0.2.15: a DELETE+pad SHIFT hold ends by itself once DELETE is up and the pad press is over: drain it (inputs as they are) */
    for (int i = 0; i < 600 && (FS.sh_on[0] || FS.sh_on[1] || FS.rp[0] || FS.rp[1]) && !idl[0] && !idl[1]; i++) step();
}
int main(void){
    prewarm_filter();
    assert(FS.snap==0 && FS.prev==0 && !FS.req && !FS.rp[0] && !FS.rp[2]);
    FS.cfg = CFG_ENABLE|CFG_CLEAR|CFG_HOTCUE;
    FS.rp_frames = 40;
    idle();

    /* SHIFT+MEMORY: counted, eaten */
    ish[0]=1; step(); imm[0]=1; step();
    assert(!HAS_MM(0) && nibv(0,0)==1 && nibv(0,12)==1);
    idle();

    /* SHIFT+pad, every pad, both sides: pad eaten, PAD counted with the kind, no DELETE leaks */
    for (int s=0;s<2;s++) for (int b=0;b<8;b++) {
        idle(); ish[s]=1; step();
        unsigned p0 = nibv(s,8);
        ipad[s]=1<<b; step();
        assert(PAD(s)==0 && HAS_SH(s));
        assert(nibv(s,8)==((p0+1)&15u) && nibv(s,24)==(unsigned)(b+1));
        assert(!HAS_DL(s));
    }
    idle();

    /* plain pad: untouched, PRESS only */
    { unsigned p0=nibv(0,12), q0=nibv(0,8); ipad[0]=1; step();
      assert(PAD(0)==1 && !HAS_SH(0) && nibv(0,12)==((p0+1)&15u) && nibv(0,8)==q0); }
    idle();

    /* DELETE alone, no pad: eaten while held, replayed as a tap on release, then gone */
    idl[0]=1; step(); assert(!HAS_DL(0)); step(); assert(!HAS_DL(0));
    idl[0]=0; step();                                    /* released: tap replay starts */
    assert(HAS_DL(0) && FS.rp[2]); step(); assert(HAS_DL(0));
    expire(); step();
    assert(!HAS_DL(0) && !FS.rp[2] && !FS.rp[0]);
    /* the other deck's DELETE tap */
    idl[1]=1; step(); assert(!HAS_DL(1)); idl[1]=0; step(); assert(HAS_DL(1) && !HAS_DL(0));
    expire(); idle();

    /* DELETE + pad: DELETE eaten, SHIFT set with the pad (stock sees SHIFT+pad), the pad passes */
    idl[0]=1; step(); ipad[0]=4; step();
    assert(!HAS_DL(0) && HAS_SH(0) && PAD(0)==0);      /* SHIFT leads the pad by one frame */
    step(); assert(!HAS_DL(0) && HAS_SH(0) && PAD(0)==4);
    ipad[0]=0; step(); assert(HAS_SH(0) && PAD(0)==4 && !HAS_DL(0));   /* finger up: the replay still delivers the whole press (0.2.13) */
    for (int i = 0; i < 60; i++) step();
    assert(HAS_SH(0) && PAD(0)==0 && !HAS_DL(0));        /* 0.2.15: the press is over, SHIFT stays down for the whole DELETE hold (one SHIFT press) */
    idl[0]=0; step();                                    /* DELETE released after a pad: NO tap; SHIFT released once */
    assert(!HAS_SH(0) && PAD(0)==0 && !HAS_DL(0) && !FS.rp[2] && !FS.rp[0] && !FS.sh_on[0]);
    /* 0.2.15: three pads under ONE DELETE hold: SHIFT is down from the first pad to the DELETE release, never up in between */
    idle(); idl[0]=1; step();
    for (int k = 0; k < 3; k++) {
        ipad[0] = (unsigned)(1u << k); step(); assert(HAS_SH(0)); step(); assert(HAS_SH(0) && (PAD(0) & (1u << k)) && !HAS_DL(0));
        ipad[0] = 0; for (int i = 0; i < 60; i++) { step(); assert(HAS_SH(0) && !HAS_DL(0)); }
    }
    idl[0]=0; step(); assert(!HAS_SH(0) && PAD(0)==0 && !FS.sh_on[0] && !HAS_DL(0)); idle();
    /* ... pad released after DELETE: same */
    idl[0]=1; step(); ipad[0]=128; step(); assert(HAS_SH(0) && PAD(0)==0); step(); assert(HAS_SH(0) && PAD(0)==128);
    idl[0]=0; step(); assert(PAD(0)==128 && HAS_SH(0) && !HAS_DL(0));   /* DELETE up first: the press is still being delivered, no DELETE leaks */
    ipad[0]=0; step(); assert(!FS.rp[2] && !HAS_DL(0)); expire(); idle();
    /* DELETE+pad on B */
    idl[1]=1; step(); ipad[1]=2; step(); assert(HAS_SH(1) && PAD(1)==0 && !HAS_DL(1)); step(); assert(HAS_SH(1) && PAD(1)==2 && !HAS_DL(1)); idle(); expire(); idle();
    /* a pad pressed first, DELETE after: stock sees the pad plain, DELETE still eaten */
    ipad[0]=1; step(); idl[0]=1; step(); assert(!HAS_DL(0)); idle(); expire(); idle();

    /* SHIFT+DELETE: eaten + CLR counted, and NO tap replay on release */
    ish[0]=1; step(); idl[0]=1; step();
    assert(!HAS_DL(0) && nibv(0,4)==1);
    idl[0]=0; step(); assert(!HAS_DL(0) && !FS.rp[2]);
    ish[0]=0; step(); assert(!HAS_DL(0) && !FS.rp[2]);
    /* DELETE held, then SHIFT joins: no tap afterwards, no delete leaked */
    idle(); idl[0]=1; step(); ish[0]=1; step(); assert(!HAS_DL(0)); idl[0]=0; ish[0]=0; step(); assert(!HAS_DL(0) && !FS.rp[2]);
    idle();

    /* replay request from the tick: SHIFT + the pad for the window, on that side only, then idle */
    FS.req = RQ_VALID | (1u<<9) | 0x04u;
    step();
    assert(PAD(1)==0 && HAS_SH(1) && !HAS_SH(0) && PAD(0)==0 && FS.req==0);   /* SHIFT leads by one frame (SHIFT is not physically held) */
    step(); assert(PAD(1)==4 && HAS_SH(1) && !HAS_SH(0));
    step(); assert(PAD(1)==4);
    expire(); step();
    assert(PAD(1)==0 && !HAS_SH(1) && !FS.rp[1]);
    /* replay while the physical SHIFT is still held and the pad was eaten */
    idle(); ish[0]=1; step(); ipad[0]=1; step(); assert(PAD(0)==0);
    FS.req = RQ_VALID | 0x01u; step();
    assert(PAD(0)==0 && HAS_SH(0));                      /* 0.2.13: the replayed pad follows the lead (1 frame here) */
    step(); assert(PAD(0)==1 && HAS_SH(0));
    expire(); idle();

    /* row off (CFG_ENABLE clear): total pass-through, even with DELETE+pad / SHIFT+pad */
    FS.cfg = CFG_CLEAR|CFG_HOTCUE;
    idle(); idl[0]=1; ipad[0]=8; step();
    assert(HAS_DL(0) && !HAS_SH(0) && PAD(0)==8);
    idle(); ish[1]=1; ipad[1]=16; imm[1]=1; idl[1]=1; step();
    assert(HAS_SH(1) && PAD(1)==16 && HAS_MM(1) && HAS_DL(1));
    idle();
    FS.cfg = CFG_ENABLE|CFG_CLEAR|CFG_HOTCUE;
    idle(); expire(); idle();

    /* (7) frame-count replay: the length is in FRAMES (rp_frames), exact to a frame, no clock involved */
    idle(); FS.rp_frames = 10;
    { unsigned long c0 = nclk, n0 = nsteps;
      idl[0]=1; step(); idl[0]=0; step();                 /* release: replay starts on this frame */
      int n = 0; while (HAS_DL(0) && n < 100) { n++; step(); }
      assert(n >= 9 && n <= 11);                           /* 10 frames +- the start frame */
      assert(!HAS_DL(0) && !FS.rp[2]);
      FS.req = RQ_VALID | 0x02u; step(); step(); n = 0; while (PAD(0) == 2 && n < 100) { n++; step(); }
      assert(n >= 8 && n <= 11 && !HAS_SH(0));
      /* every call reads the counter exactly twice (entry, exit), nothing else */
      assert(nclk - c0 == 2 * (nsteps - n0)); }
    FS.rp_frames = 40; idle();
    /* (7) 5000 frames of every kind of input: exactly two counter reads per call, on every path */
    { unsigned long c0 = nclk, n0 = nsteps;
      for (int i = 0; i < 5000; i++) {
          ish[i&1] = (i % 7) < 2; imm[i&1] = (i % 11) == 3; idl[(i>>1)&1] = (i % 5) == 1; ipad[i&1] = (unsigned)(1u << (i & 7)) * ((i % 3) == 0);
          step();
          if ((i % 97) == 0) FS.req = RQ_VALID | (unsigned)(i & 0xff) | ((i & 1) << 9);
      }
      assert(nclk - c0 == 2 * (nsteps - n0)); }
    idle(); expire(); idle();
    /* idle frames: two counter reads per call */
    { unsigned long c0 = nclk, n0 = nsteps; for (int i = 0; i < 2560; i++) step(); assert(nclk - c0 == 2 * (nsteps - n0)); }

    /* idle frames cost the fast path only: nothing changes */
    for (int i=0;i<1000;i++) step();
    assert(!FS.rp[0] && !FS.rp[2] && !FS.dh[0]);

    /* (8) al028c: DELETE-only tap diagnostic, frame stamp */
    idle(); expire(); idle(); FS.rp_frames = 40;
    { uint32_t tp0 = FS.tap;
      idl[0]=1; for (int i=0;i<7;i++) step(); idl[0]=0; step();
      assert((FS.tap >> 16) == (tp0 >> 16) + 1 && ((FS.tap >> 8) & 1) == 0 && (FS.tap & 0xff) >= 7 && (FS.tap & 0xff) <= 9);
      expire(); idle();
      tp0 = FS.tap;                                    /* DELETE then a pad, then both up: not a tap */
      idl[1]=1; step(); step(); ipad[1]=2; step(); step(); ipad[1]=0; step(); idl[1]=0; step();
      assert(FS.tap == tp0 && !FS.rp[3]);
      idle(); expire(); idle();
      idl[1]=1; step(); idl[1]=0; step();              /* a 1-frame DELETE blip (what a coalesced press looks like) is replayed */
      assert((FS.tap >> 16) == (tp0 >> 16) + 1 && ((FS.tap >> 8) & 1) == 1 && FS.rp[3]);
      expire(); idle(); }
    { uint64_t st0 = FS.stamp; for (int i=0;i<1024;i++) step();
      assert((((FS.stamp >> 40) - (st0 >> 40)) & 0xffffff) == 4 && (FS.stamp & STAMP_MASK) != (st0 & STAMP_MASK)); }
    assert(FS.last_t != 0 && FS.fmax != 0);

    /* (9) al028c fixes (fuzz-found): a new DELETE press ends an older tap replay; SHIFT joining a DELETE hold
     * means "no tap" even if SHIFT leaves first; frames missing (a stall) inside a DELETE-alone hold = no tap */
    idle(); expire(); idle(); FS.rp_frames = 40;
    { idl[0]=1; step(); idl[0]=0; step(); assert(FS.rp[2] && HAS_DL(0));               /* a tap is being replayed ... */
      idl[0]=1; step(); assert(!HAS_DL(0) && !FS.rp[2]);                                /* ... a new press ends it, DELETE eaten */
      ipad[0]=2; step(); step(); assert(!HAS_DL(0) && HAS_SH(0) && PAD(0)==2);
      for (int i=0;i<60;i++){ step(); assert(!HAS_DL(0)); }                            /* no DELETE leaks while DELETE+pad is held */
      ipad[0]=0; idl[0]=0; step(); assert(!HAS_DL(0) && !FS.rp[2]); idle(); }
    { uint32_t tp0 = FS.tap;                                                           /* SHIFT joins, SHIFT leaves first, DELETE last */
      idl[0]=1; step(); ish[0]=1; step(); ish[0]=0; step(); step(); idl[0]=0; step();
      assert(FS.tap == tp0 && !FS.rp[2] && !HAS_DL(0)); idle(); }
    { uint32_t tp0 = FS.tap, tx0 = FS.tapx;                                            /* a stall inside the hold: not guessed as a tap */
      idl[0]=1; step(); step(); FS.last_t = cnt_now() - 3 * (uint64_t)FS.gthr; step(); idl[0]=0; step();
      assert(FS.tap == tp0 && FS.tapx == tx0 + 1 && !FS.rp[2] && !HAS_DL(0)); idle();
      FS.last_t = cnt_now() - 3 * (uint64_t)FS.gthr; idl[0]=1; step(); step(); idl[0]=0; step();     /* hold that starts right after a stall */
      assert(FS.tap == tp0 && FS.tapx == tx0 + 2 && !HAS_DL(0)); idle();
      idl[0]=1; step(); step(); idl[0]=0; step();                                      /* no stall: still a tap */
      assert((FS.tap >> 16) == (tp0 >> 16) + 1 && HAS_DL(0)); expire(); idle(); }
    { uint32_t d0 = FS.dlp; idl[1]=1; step(); ipad[1]=16; step(); assert((FS.dlp >> 16) == (d0 >> 16) + 1 && ((FS.dlp >> 8) & 1) == 1 && (FS.dlp & 0xff) == 5); idle(); expire(); idle(); }
    { uint32_t e0 = FS.nedge; for (int i=0;i<500;i++) step(); assert(FS.nedge == e0);   /* idle frames never take the full path */
      ipad[0]=1; step(); assert(FS.nedge > e0); idle(); }

    /* (10) al028d: a pad that went down under SHIFT stays eaten until it is up (SHIFT up first: no new plain press) */
    idle(); expire(); idle(); FS.emp = 0;
    { ish[0]=1; step(); ipad[0]=4; step(); assert(PAD(0)==0);
      ish[0]=0; step(); assert(PAD(0)==0 && !HAS_SH(0));                 /* SHIFT released, pad still down: still eaten */
      step(); assert(PAD(0)==0);
      ipad[0]=0; step(); assert(PAD(0)==0);
      ipad[0]=4; step(); assert(PAD(0)==4 && !HAS_SH(0)); idle(); }      /* a fresh plain press passes */
    { ipad[1]=8; step(); ish[1]=1; step(); assert(PAD(1)==0);            /* pad first, SHIFT second: stock saw the press; nothing is held back */
      ish[1]=0; step(); assert(PAD(1)==8); idle(); }
    /* (11) al028d: DELETE + pad on an EMPTY pad (emp mask from the tick): swallowed, never a SHIFT+pad, and it stays eaten */
    { FS.emp = 0x02u | (0x10u << 8);                                      /* side A pad 2 empty, side B pad 5 empty */
      uint32_t d0 = FS.dlp, e0 = FS.dle;
      idl[0]=1; step(); ipad[0]=2; step();
      assert(PAD(0)==0 && !HAS_SH(0) && !HAS_DL(0) && (FS.dle >> 16) == (e0 >> 16) + 1 && (FS.dle & 0xff) == 2 && FS.dlp == d0);
      step(); assert(PAD(0)==0 && !HAS_SH(0));
      idl[0]=0; step(); assert(PAD(0)==0 && !HAS_SH(0) && !HAS_DL(0));   /* DELETE up first, pad still down: still swallowed, no tap */
      ipad[0]=0; step(); assert(PAD(0)==0 && !HAS_DL(0)); idle();
      /* a pad that is NOT in the mask under DELETE is still DELETE+pad (SHIFT lead, then SHIFT+pad) */
      idl[0]=1; step(); ipad[0]=4; step(); assert(PAD(0)==0 && HAS_SH(0)); step(); assert(PAD(0)==4 && HAS_SH(0) && FS.dlp != d0);
      ipad[0]=0; idl[0]=0; step(); idle(); expire(); idle();
      idl[1]=1; step(); ipad[1]=16; step(); assert(PAD(1)==0 && !HAS_SH(1) && (FS.dle & 0xff) == 5 && ((FS.dle >> 8) & 1) == 1);
      ipad[1]=0; idl[1]=0; step(); idle();
      FS.emp = 0;                                                         /* unknown (tick not run yet): the old behaviour */
      idl[0]=1; step(); ipad[0]=2; step(); assert(HAS_SH(0)); idle(); expire(); idle(); }
    /* (12) al028d: the stall test follows the frame interval (17 / 125 / 1000 frames/s on a virtual clock: see filter_fuzz) */
    { uint64_t fi0 = FS.fi; assert(FS.fn >= FI_MIN && fi0 > 0 && FS.gthr > 0); }

    { uint64_t t0=now_ns(), worst=0; for(int i=0;i<200000;i++){ uint64_t a=now_ns(); memset(F,0,sizeof F); F[A_PAD_O]=(unsigned char)(i&1); F[A_SH_O]=(unsigned char)((i&2)<<6); on_erp(AZ_BUS_ERP_IN,F,ERP_LEN,NULL); uint64_t d=now_ns()-a; if(i>10&&d>worst) worst=d; } printf("avg %.0f ns/call, worst %llu ns (incl. 2 clock reads)\n",(now_ns()-t0)/200000.0,(unsigned long long)worst); }
    printf("filter_test: ok\n");
    return 0;
}
