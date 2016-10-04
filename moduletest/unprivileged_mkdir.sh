#!/bin/sh -e

# Shall fail
! XDG_RUNTIME_DIR=/root "$1" run true 2> /dev/null
! XDG_RUNTIME_DIR=/root "$1" build true 2> /dev/null
