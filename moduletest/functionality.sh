#!/bin/sh -e
test "$XDG_RUNTIME_DIR" = '' && XDG_RUNTIME_DIR=/tmp

test "$(selfdock run ps -a | tail -n +2)" = '    1 ?        00:00:00 ps'

test "$(selfdock run selfdock run wc -l /proc/mounts)" = '4 /proc/mounts'
