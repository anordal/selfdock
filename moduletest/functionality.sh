#!/bin/sh -e
test "$XDG_RUNTIME_DIR" = '' && XDG_RUNTIME_DIR=/tmp

test "$("$1" run ps -a | tail -n +2)" = '    1 ?        00:00:00 ps'

test "$("$1" run "$1" run wc -l /proc/mounts)" = '4 /proc/mounts'
