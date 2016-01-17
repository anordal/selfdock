#!/bin/sh
test "$XDG_RUNTIME_DIR" = '' && XDG_RUNTIME_DIR=/tmp

selfdock / "$1" sh -c 'exit 42'
test $? = 42 || exit 1

selfdock / "$1" / 2>/dev/null
test $? = 126 || exit 1

selfdock / "$1" /proc/mounts 2>/dev/null
test $? = 126 || exit 1

selfdock / "$1" /dev/null 2>/dev/null
test $? = 126 || exit 1

selfdock / "$1" /1 2>/dev/null
test $? = 127 || exit 1

selfdock / "$1" env PATH= true 2>/dev/null
test $? = 127 || exit 1
