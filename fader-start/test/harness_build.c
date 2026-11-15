/* TEST-ONLY: the SDK harness requires fw_tags {"any", NULL} and refuses claim_hook, so this
 * builds fader-start with AZ_HARNESS: no hooks and no frame filters (the harness core has
 * none); tick, page, worker logger, deck events and re-init are exercised. Never shipped. */
#define AZ_HARNESS 1
#include "../fader-start.c"
