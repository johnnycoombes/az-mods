/* 0.2.4: JC's new export (read-only copy): loops and rekordbox Active Loop flags per track,
 * through the same EXT+DAT path the mod uses at load. */
#include "../active-loop.c"
#include <assert.h>
static const struct { const char *name, *dir; } T[] = {
    { "Cola",      "P01E/0000CADE" }, { "Hold On",  "P021/0002E903" },
    { "Good Life", "P03C/000023D2" }, { "The Fog",  "P078/000172A8" },
};
int main(int argc, char **argv){
    const char *root = argc > 1 ? argv[1] : "/workspace/mod-test/al023anlz/USBANLZ";
    int ok = 1;
    for (unsigned t = 0; t < sizeof T / sizeof T[0]; t++) {
        static anlz_res_t r; char dir[512];
        memset(&r, 0, sizeof r);
        snprintf(dir, sizeof dir, "%s/%s", root, T[t].dir);
        int rc = anlz_dir_loops(dir, &r); al_dedup(r.e, &r.n);
        int f = 0, fi = -1;
        for (int i = 0; i < r.n; i++) if (r.e[i].flag) { f++; if (fi < 0) fi = i; }
        printf("%-9s rc=%d loops=%d flagged=%d", T[t].name, rc, r.n, f);
        if (fi >= 0) printf("  flagged: hot=%d %u-%u ms -> in=%lld out=%lld (44.1/ms)", r.e[fi].hot, r.e[fi].in_ms, r.e[fi].out_ms,
                            (long long)(r.e[fi].in_ms * 44.1 + 0.5), (long long)(r.e[fi].out_ms * 44.1 + 0.5));
        printf("\n");
        if (rc != 0 || f != 1) ok = 0;
    }
    return ok ? 0 : 1;
}
