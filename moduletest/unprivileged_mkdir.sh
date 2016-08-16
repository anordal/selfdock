#!/bin/sh -e

# Shall fail
! XDG_RUNTIME_DIR=/root "$1" run true 2> /dev/null
