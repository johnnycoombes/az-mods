#!/bin/sh
# Wrapper used on the host / for aarch64 -Werror builds: the 0.2.17 tree already
# had format-truncation in overview log lines. Do not treat those as errors.
exec "${REAL_CC:-gcc}" -Wno-error=format-truncation "$@"
