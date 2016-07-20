#!/bin/bash
set -e
test "$XDG_RUNTIME_DIR" = '' && XDG_RUNTIME_DIR=/tmp

s=( selfdock run )
s4=( "${s[@]}" "${s[@]}" "${s[@]}" "${s[@]}" )
s16=( "${s4[@]}" "${s4[@]}" "${s4[@]}" "${s4[@]}" )

"${s16[@]}" true

! "${s[@]}" cp /bin/sh / 2>/dev/null
! "${s[@]}" cp /bin/sh /dev/ 2>/dev/null
! "${s4[@]}" cp /bin/sh / 2>/dev/null
! "${s4[@]}" cp /bin/sh /dev/ 2>/dev/null
! "${s16[@]}" cp /bin/sh / 2>/dev/null
! "${s16[@]}" cp /bin/sh /dev/ 2>/dev/null

t=( sh -ec 'echo fail > /dev/null && cat /dev/null' )

test "$("${s[@]}" "${t[@]}")" = ''
test "$("${s4[@]}" "${t[@]}")" = ''
test "$("${s16[@]}" "${t[@]}")" = ''
