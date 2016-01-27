#!/bin/sh -e

# Shall fail
! XDG_RUNTIME_DIR=/root selfdock / "$1" true 2> /dev/null
