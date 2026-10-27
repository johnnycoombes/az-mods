/* TEST-ONLY harness variant (make test-harness). Not shipped: SRCS is tempo-range.c.
 * Sets fw_tags to {"any",NULL} and skips hook claims so the stock SDK harness can run;
 * hook bodies are covered by unit tests and the emulator/deck instead. */
#define AZ_HARNESS 1
#include "../tempo-range.c"
