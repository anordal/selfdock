#!/bin/sh -e
test "$XDG_RUNTIME_DIR" = '' && XDG_RUNTIME_DIR=/tmp

test "$(selfdock / moduletest ps -a | tail -n +2)" = '    1 ?        00:00:00 ps'

test "$(selfdock / moduletest wc -l /proc/mounts)" = '2 /proc/mounts'
