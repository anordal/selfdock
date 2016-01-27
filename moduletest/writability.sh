#!/bin/sh -e
test "$XDG_RUNTIME_DIR" = '' && XDG_RUNTIME_DIR=/tmp

! selfdock / "$1" cp /bin/sh / 2>/dev/null
! selfdock / "$1" cp /bin/sh /dev/ 2>/dev/null

test "$(selfdock / "$1" sh -ec 'echo fail > /dev/null && cat /dev/null')" = ''
