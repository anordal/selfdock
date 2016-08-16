#!/bin/sh
test "$XDG_RUNTIME_DIR" = '' && XDG_RUNTIME_DIR=/tmp

"$1" run sh -c 'exit 42'
test $? = 42 || exit 1

"$1" run / 2>/dev/null
test $? = 126 || exit 1

"$1" run /proc/mounts 2>/dev/null
test $? = 126 || exit 1

"$1" run /dev/null 2>/dev/null
test $? = 126 || exit 1

"$1" run /1 2>/dev/null
test $? = 127 || exit 1

"$1" run env PATH= true 2>/dev/null
test $? = 127 || exit 1
