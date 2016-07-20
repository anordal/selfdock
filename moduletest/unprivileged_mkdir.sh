#!/bin/sh -e

# Shall fail
! XDG_RUNTIME_DIR=/root selfdock run true 2> /dev/null
