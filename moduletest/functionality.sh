#!/bin/sh
set -e

test "$("$1" run ps -a | tail -n +2)" = '    1 ?        00:00:00 ps'

test "$("$1" run "$1" run wc -l /proc/mounts)" = '5 /proc/mounts'
