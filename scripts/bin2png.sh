#!/bin/sh

FILE=${1:-.}

convert_image() {
	local file=$1
	local pnm=${file%bin}pnm
	local png=${file%bin}png

	local format=$(echo $file | sed -e 's|.*-\([[:alnum:]]*\)-\([0-9]*x[0-9]*\).*.bin|\1|' | tr '[:lower:]' '[:upper:]')
	local size=$(echo $file   | sed -e 's|.*-\([[:alnum:]]*\)-\([0-9]*x[0-9]*\).*.bin|\2|')

	case $format in
	YUV*|YVU*)
		format=$(echo $format | tr 'M' 'P')
		;;
	NV*)
		format=$(echo $format | tr -d 'M')
		;;
	*RGB*)
		format=$(echo $format | tr -d 'AX')
		;;
	esac

	raw2rgbpnm -f $format -s $size $file $pnm && \
		convert $pnm $png
	rm $pnm
}

if [ -d $FILE ] ; then
	if [ $(ls $FILE/vsp-unit-test-00*-*frame*.bin 2>/dev/null | wc -l) != 0 ] ; then
		for f in $FILE/vsp-unit-test-00*-*frame*.bin ; do
			convert_image $f
		done
	fi
elif [ -f $FILE ] ; then
	convert_image $FILE
else
	echo "Usage: $0 <file or directory>"
fi
