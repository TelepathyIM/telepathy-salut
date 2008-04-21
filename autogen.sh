#!/bin/sh
set -e

if test -z "$MAKE"
then
	MAKE=make
fi

autoreconf -i

run_configure=true
for arg in $*; do
    case $arg in
        --no-configure)
            run_configure=false
            ;;
        *)
            ;;
    esac
done

if test $run_configure = true; then
    ./configure "$@"
fi
