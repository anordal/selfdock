#!/bin/sh
test "$XDG_RUNTIME_DIR" = '' && XDG_RUNTIME_DIR=/tmp

selfdock run sh -c 'exit 42'
test $? = 42 || exit 1

selfdock run / 2>/dev/null
test $? = 126 || exit 1

selfdock run /proc/mounts 2>/dev/null
test $? = 126 || exit 1

selfdock run /dev/null 2>/dev/null
test $? = 126 || exit 1

selfdock run /1 2>/dev/null
test $? = 127 || exit 1

selfdock run env PATH= true 2>/dev/null
test $? = 127 || exit 1
