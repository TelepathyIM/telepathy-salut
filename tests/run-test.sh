#!/bin/sh

mkdir -p outputs

for x in ./inputs/*.input ; do
  FILE=$(basename ${x%.input})
  XMLOUT="outputs/${FILE}.output"
  PARSEOUT="outputs/${FILE}.parse"
  ./test-xmpp-connection $x ${PARSEOUT} ${XMLOUT} 
  if [ ! $? ] ; then
    echo "FAILED: $x - Test program had non-zero exit status" >&2
    continue
  fi
  xmldiff $x $XMLOUT
  if [ ! $? ] ; then
    echo "FAILED: $x - XML output doesn't match the input" >&2
    continue
  fi
  echo "SUCCESS: $x" >&2
done
