#!/bin/sh
set -e

test "$("$1" run ps -a | tail -n +2)" = $'    1 ?        00:00:00 selfdock\n    2 ?        00:00:00 ps'

test "$("$1" run "$1" run wc -l /proc/mounts)" = '5 /proc/mounts'

cleanup()
{
	rmdir /tmp/touchme
}
trap cleanup EXIT

"$1" run mkdir /tmp/touchme
! test -e /tmp/touchme
"$1" -v /tmp /tmp run mkdir /tmp/touchme
"$1" -m /tmp /tmp run test -e /tmp/touchme
