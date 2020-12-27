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

has_mnt_opt=( sh -c 'while read dev mnt type opt dump pass; do test "$mnt" = "$0" || continue; for i in ${opt//,/ }; do test "$i" = "$1" && exit 0; done; done < /proc/mounts; false' )

"$1" build "${has_mnt_opt[@]}" / rw
! "$1" run "${has_mnt_opt[@]}" / rw
! "$1" build "$1" run "${has_mnt_opt[@]}" / rw
! "$1" run "$1" build "${has_mnt_opt[@]}" / rw

"$1" run "${has_mnt_opt[@]}" /tmp rw
! "$1" run "${has_mnt_opt[@]}" /usr rw
! "$1" -m /usr /usr run "${has_mnt_opt[@]}" /usr rw
"$1" -v /usr /usr run "${has_mnt_opt[@]}" /usr rw

"$1" run "${has_mnt_opt[@]}" /tmp size=2048k
"$1" -t size=4K /tmp run "${has_mnt_opt[@]}" /tmp size=4k
