#!/bin/sh -e
test "$XDG_RUNTIME_DIR" = '' && XDG_RUNTIME_DIR=/tmp

test "$(selfdock / "$1" ps -a | tail -n +2)" = '    1 ?        00:00:00 ps'

test "$(selfdock / "$1" selfdock / "$1" wc -l /proc/mounts)" = '4 /proc/mounts'
