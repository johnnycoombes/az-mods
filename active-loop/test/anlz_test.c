/* 0.2.2: rekordbox Active Loop flag (PCPT status 4) from real ANLZ DAT files (read-only). */
#include "../active-loop.c"
#include <assert.h>
#include <glob.h>
int main(int argc, char **argv){
    glob_t g; int files = 0, one = 0;
    glob(argc > 1 ? argv[1] : "/workspace/dexport/f/*.DAT", 0, NULL, &g);
    for (size_t k = 0; k < g.gl_pathc; k++) {
        al_t e[NAL]; int n = 0, fl = 0;
        long len = slurp(g.gl_pathv[k], anlz_buf, sizeof anlz_buf);
        if (len <= 0 || anlz_loops(anlz_buf, len, e, NAL, &n) != 0 || !n) continue;
        al_dedup(e, &n);
        for (int i = 0; i < n; i++) fl += e[i].flag;
        files++; one += fl == 1;
    }
    globfree(&g);
    printf("DAT files with loops: %d, with exactly one rekordbox Active Loop flag: %d\n", files, one);
    return files && one == files ? 0 : 1;
}
