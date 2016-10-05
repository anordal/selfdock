#!/bin/bash
set -e

s=( "$1" run )
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

mount_rw_check=( sh -c 'while read dev mnt type opt dump pass; do test "$mnt" = / || continue; for i in ${opt//,/ }; do test "$i" = rw && exit 0; done; done < /proc/mounts; false' )

"$1" build "${mount_rw_check[@]}"
! "$1" run "${mount_rw_check[@]}"
! "$1" build "$1" run "${mount_rw_check[@]}"
! "$1" run "$1" build "${mount_rw_check[@]}"
