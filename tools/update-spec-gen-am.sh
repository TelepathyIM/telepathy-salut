#!/bin/sh

mk_specdir="extensions"
mk_toolsdir="extensions/tools"

test -n "$XSLTPROC" || XSLTPROC=xsltproc
test -n "$TOP_SRCDIR" || TOP_SRCDIR=..

toolsdir="tools"
specdir="."

outfile="$1"
gendir="$2"
whitelist="$3"

SPEC_INTERFACES="`$XSLTPROC --nonet --novalid --xinclude $toolsdir/ls-interfaces.xsl $specdir/all.xml`"

install -d ./`dirname "$outfile"`
exec > "$outfile.tmp"

echo "# Rules to re-generate this file"
printf "$outfile: \$(top_srcdir)/$mk_specdir/all.xml \\\\\\n"
printf "\\t\\t\$(top_srcdir)/$mk_specdir/all.xml \\\\\\n"
printf "\\t\\t\$(SPEC_INTERFACE_XMLS) \\\\\\n"
printf "\\t\\t\$(top_srcdir)/$mk_toolsdir/ls-interfaces.xsl \\\\\\n"
printf "\\t\\t\$(top_builddir)/$mk_toolsdir/update-spec-gen-am.sh\\n"
printf "\\tXSLTPROC=xsltproc TOP_SRCDIR=\$(top_srcdir) \$(SHELL) \$(top_builddir)/$mk_toolsdir/update-spec-gen-am.sh $outfile $gendir $whitelist\\n"
echo

for class in INTERFACES INTERFACE_XMLS GENERATED_CS GENERATED_HS \
       GENERATED_LISTS GLUE_HS
do
  if test -z "$whitelist"
  then
    echo "SPEC_$class ="
  else
    echo "STABLE_SPEC_$class ="
    echo "UNSTABLE_SPEC_$class ="
  fi
done

for iface in $SPEC_INTERFACES
do
  if test -z "$whitelist"
  then
    # just output the combined variables directly
    stability=
  elif grep "^$iface\$" "$whitelist" >/dev/null
  then
    stability=STABLE_
  else
    stability=UNSTABLE_
  fi
  echo "${stability}SPEC_INTERFACES += $iface"
  echo "${stability}SPEC_INTERFACE_XMLS += \$(top_srcdir)/$mk_specdir/$iface.xml"
  if test -n "$gendir"
  then
    echo "${stability}SPEC_GENERATED_CS += $gendir/svc-$iface.c"
    echo "${stability}SPEC_GENERATED_HS += $gendir/svc-$iface.h"
    echo "${stability}SPEC_GLUE_HS += $gendir/svc-$iface-glue.h"
    echo "${stability}SPEC_GENERATED_LISTS +="\
         "$gendir/svc-$iface-signals-marshal.list"
  fi
done

if test -n "$whitelist"
then
  for class in INTERFACES INTERFACE_XMLS GENERATED_CS GENERATED_HS \
               GENERATED_LISTS GLUE_HS
  do
    echo "SPEC_$class = \$(STABLE_SPEC_$class) \$(UNSTABLE_SPEC_$class)"
  done
fi

mv "$outfile.tmp" "$outfile"
