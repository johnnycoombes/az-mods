/* TEST-ONLY: deck-select filter (0.1.14). Feeds ERP_IN frames, checks the latched layer. */
#define AZ_HARNESS 1
#include "../tempo-range.c"
#include <assert.h>
static unsigned char f[30];
static void press(int side, int sec)
{
    memset(f, 0, sizeof f); f[8 + 8 * side + (sec ? 1 : 0)] = 1; on_erp_sel(AZ_BUS_ERP_IN, f, 30, NULL);
    memset(f, 0, sizeof f); on_erp_sel(AZ_BUS_ERP_IN, f, 30, NULL);
}
int main(void)
{
    memset(f, 0, sizeof f); on_erp_sel(AZ_BUS_ERP_IN, f, 30, NULL);
    assert((sel_word & 0xf) == 0);                 /* nothing known yet */
    press(1, 1); assert((sel_word & 0xc) == 0xc);  /* right side -> deck 4 */
    press(0, 1); assert((sel_word & 0x3) == 0x3);  /* left -> deck 3 */
    press(1, 0); assert((sel_word & 0xc) == 0x4);  /* right -> deck 2 */
    memset(f, 0, sizeof f); f[9] = 1; on_erp_sel(AZ_BUS_ERP_IN, f, 30, NULL);
    on_erp_sel(AZ_BUS_ERP_IN, f, 30, NULL);        /* held: one edge only */
    assert((sel_word & 0x3) == 0x3);
    assert((sel_word >> 8) == 9);                  /* heartbeat counts every frame */
    assert(on_erp_sel(AZ_BUS_ERP_IN, NULL, 0, NULL) == AZ_FRAME_OK);
    puts("sel_test: OK");
    return 0;
}
