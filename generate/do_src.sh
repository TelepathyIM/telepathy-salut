#!/bin/sh

set -e

PYVER=2.4
PYTHON=python$PYVER

if [ `basename $PWD` == "generate" ]; then
  TP=${TELEPATHY_SPEC:=$PWD/../../telepathy-spec}
else
  TP=${TELEPATHY_SPEC:=$PWD/../telepathy-spec}
fi

export PYTHONPATH=$TP:$PYTHONPATH

if test -d generate; then cd generate; fi
cd src

echo Generating SalutConnectionManager files ...
$PYTHON $TP/tools/gengobject.py ../xml-modified/salut-connection-manager.xml SalutConnectionManager

echo Generating SalutConnection files ...
$PYTHON $TP/tools/gengobject.py ../xml-modified/salut-connection.xml SalutConnection

#echo Generating SalutContactChannel files ...
$PYTHON $TP/tools/gengobject.py ../xml-modified/salut-contact-channel.xml SalutContactChannel

echo Generating SalutIMChannel files ...
$PYTHON $TP/tools/gengobject.py ../xml-modified/salut-im-channel.xml SalutIMChannel
#
#echo Generating SalutMucChannel files ...
#$PYTHON $TP/tools/gengobject.py ../xml-modified/salut-muc-channel.xml SalutMucChannel

#echo Generating SalutMediaChannel files ...
#$PYTHON $TP/tools/gengobject.py ../xml-modified/salut-media-channel.xml SalutMediaChannel

#echo Generating SalutMediaSession files ...
#$PYTHON $TP/tools/gengobject.py ../xml-modified/salut-media-session.xml SalutMediaSession

#echo Generating SalutMediaStream files ...
#$PYTHON $TP/tools/gengobject.py ../xml-modified/salut-media-stream.xml SalutMediaStream

#echo Generating SalutRoomlistChannel files ...
#$PYTHON $TP/tools/gengobject.py ../xml-modified/salut-roomlist-channel.xml SalutRoomlistChannel

echo Generating error enums ...
$PYTHON $TP/tools/generrors.py
