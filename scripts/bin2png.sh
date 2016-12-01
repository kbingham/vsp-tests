#!/bin/sh

FILE="$1"

PNM=${FILE/%bin/pnm}
PNG=${FILE/%bin/png}

fmt=$(echo $FILE | sed -e 's|.*-\([[:alnum:]]*\)-\([0-9]*x[0-9]*\).*.bin|\1|' | tr '[:lower:]' '[:upper:]')
size=$(echo $FILE | sed -e 's|.*-\([[:alnum:]]*\)-\([0-9]*x[0-9]*\).*.bin|\2|')

case $fmt in
	yuv*|yvu*)
		fmt=$(echo $fmt | tr 'M' 'P')
		;;
	nv*)
		fmt=$(echo $fmt | tr -d 'M')
		;;
	*rgb*)
		fmt=$(echo $fmt | tr -d 'AX')
		;;
esac

raw2rgbpnm -s $size -f $fmt $FILE $PNM && \
	convert $PNM $PNG
rm $PNM
