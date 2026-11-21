#!/bin/sh
# active-loop unit suites on the host: plain / asan+ubsan / tsan.
# Usage: test/run_unit.sh [suite ...]
cd "$(dirname "$0")/.." || exit 1
SUITES=${*:-"jog_test shift_release_test anlz_test anlz_tracks_test echeck_test ring_test filter_test deletepad_deck_test delete_test persist_test load_test led_test ovp_gate_test filter_fuzz filter_bench"}
INC=${AZ_SDK_INCLUDE:-/workspace/az-mods-sdk/include}
CC=${TEST_CC:-gcc}
rc=0
for san in plain asan tsan; do
  case $san in
    plain) F="-g -O1 -Wall -Wextra -Werror=implicit-function-declaration";;
    asan)  F="-g -O1 -Wall -Wextra -Werror=implicit-function-declaration -fsanitize=address,undefined -fno-sanitize-recover=undefined,address";;
    tsan)  F="-g -O1 -Wall -Wextra -Werror=implicit-function-declaration -fsanitize=thread";;
  esac
  for t in $SUITES; do
    echo "== $t [$san]"
    SRC=test/$t.c
    $CC $F -Wno-unused-function -Wno-format-truncation -I"$INC" -o /tmp/al_$t.$san "$SRC" -lm -lpthread || { echo BUILD FAIL; rc=1; continue; }
    if [ "$t" = filter_fuzz ]; then
      for a in "17 1 30" "125 2 30" "1000 3 30" "17 4 8" "125 5 8" "1000 6 8" "125 7 60"; do
        /tmp/al_$t.$san $a || { echo "FUZZ FAIL $a"; rc=1; }
      done
    else
      /tmp/al_$t.$san || { echo "SUITE FAIL $t $san"; rc=1; }
    fi
  done
done
exit $rc
