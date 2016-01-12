#!/bin/sh -e
test "$XDG_RUNTIME_DIR" = '' && XDG_RUNTIME_DIR=/tmp

# Shall fail
! XDG_RUNTIME_DIR=/root selfdock / moduletest true 2> /dev/null
