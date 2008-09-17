#!/bin/sh

mkdir -p outputs

RET=0


srcdir=${srdir:=$(dirname $0)}

for x in ${srcdir}/inputs/*.input ; do
  FILE=$(basename ${x%.input})
  XMLOUT="outputs/${FILE}.output"
  PARSEOUT="outputs/${FILE}.parse"
  ./test-xmpp-connection $x ${PARSEOUT} ${XMLOUT} 
  if [ $? -ne 0 ] ; then
    echo "FAILED: $x - Test program had non-zero exit status" >&2
    RET=1
    continue
  fi
  xmldiff $x $XMLOUT
  if [ $? -ne 0 ] ; then
    echo "FAILED: $x - XML output doesn't match the input" >&2
    RET=1
    continue
  fi
  echo "SUCCESS: $x" >&2
done

exit ${RET}
