/* qemu-aarch64 unit test of the 0.1.5 hook->tick ring and safe_read. */
#include "../active-loop.c"
#include <pthread.h>
#include <assert.h>
static long copies, dtors, misalign;
static void st_copy(void *d, const void *s) {
    if ((uintptr_t)d & 15) __atomic_add_fetch(&misalign, 1, __ATOMIC_RELAXED);
    uint64_t *dd = d; const uint64_t *ss = s;         /* 16-byte accesses like fw */
    for (int i = 0; i < CUE_SIZE / 8; i++) __atomic_store_n(&dd[i], ss[i], __ATOMIC_RELAXED);
    __atomic_add_fetch(&copies, 1, __ATOMIC_RELAXED);
}
static void st_dtor(void *c) { (void)c; dtors++; }
static int  st_loop(void *c) { (void)c; return 0; }
static long seen;
static void *prod(void *a) {
    static __attribute__((aligned(16))) uint8_t cue[CUE_SIZE];
    for (int i = 0; i < 20000; i++) { if (!(i & 31)) usleep(200); push_ev((void *)0x100000, (int)((uintptr_t)a + i) % 9, SRC_CALL, cue); }
    return NULL;
}
int main(void){
    al_live = 1;   /* 0.2.6: hooks are pass-through until init() completes */
    fw_cue_copy = st_copy; fw_cue_dtor = st_dtor; fw_is_loop = st_loop;
    assert(((uintptr_t)ring & 63) == 0 && ((uintptr_t)ring[1].cue & 15) == 0);
    pthread_t t[3];
    for (long i = 0; i < 3; i++) pthread_create(&t[i], NULL, prod, (void *)i);
    for (int k = 0; k < 4000000 && (__atomic_load_n(&copies, __ATOMIC_RELAXED) < 60000 || ring_r != __atomic_load_n(&ring_w, __ATOMIC_ACQUIRE)); k++) { uint32_t before = ring_r; drain_ring(); seen += ring_r - before; }
    for (int i = 0; i < 3; i++) pthread_join(t[i], NULL);
    for (int k = 0; k < 1000000 && ring_r != ring_w; k++) { uint32_t b0 = ring_r; drain_ring(); seen += ring_r - b0; }
    printf("pushed=60000 reserved=%u drained=%ld errno-probe:\n", ring_w, seen); { uint64_t x; struct iovec l={&x,8},r={&x,8}; long rc=syscall(SYS_process_vm_readv,(long)getpid(),&l,1L,&r,1L,0L); printf(" self read rc=%ld\n", rc); }
    int live = 0; for (int i = 0; i < RING; i++) if (ring[i].seq) live++;
    uint64_t v; int bad = safe_read(0x10, &v, 8), bad2 = safe_read(0x7ff0000000ull, &v, 8);
    int good = safe_read((uintptr_t)&copies, &v, 8);
    printf("copies=%ld dtors=%ld misaligned=%ld live_slots=%d safe_read bad=%d,%d good=%d sizeof(ev_t)=%zu\n",
           copies, dtors, misalign, live, bad, bad2, good, sizeof(ev_t));
    return !(misalign == 0 && copies == dtors && live == 0 && bad && bad2 && !good && seen == 60000);
}
