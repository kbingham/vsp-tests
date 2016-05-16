#!/bin/sh

set -e

source vsp-lib.sh

mediactl='media-ctl'
yavta='yavta'

# ------------------------------------------------------------------------------
# Format retrieval
#

frame_reference() {
	format=$1
	size=$2

	lcfmt=`echo $infmt | tr '[:upper:]' '[:lower:]'`

	case $format in
	ARGB555)
		echo "frames/frame-reference-$lcfmt-$size-alpha255.bin"
		;;
	ABGR32 | ARGB32)
		echo "frames/frame-reference-$lcfmt-$size-alpha200.bin"
		;;
	XRGB555 | XBGR32 | XRGB32)
		echo "frames/frame-reference-$lcfmt-$size-alpha0.bin"
		;;
	*)
		echo "frames/frame-reference-$lcfmt-$size.bin"
		;;
	esac
}

# ------------------------------------------------------------------------------
# Parse the command line and retrieve the formats
#

syntax() {
	echo "Syntax: vsp-runner.sh dev cmd [...]"
	echo ""
	echo "Supported commands:"
	echo "    hgo [options]"
	echo "    input index infmt [options]"
	echo "    output index outfmt [options]"
}

parse() {
	if [ $# -lt 2 ] ; then
		syntax
		return 1
	fi

	mdev=$1
	dev=`$mediactl -d $mdev -p | grep 'bus info' | sed 's/.*platform://'`

	if [ -z $dev ] ; then
		echo "Error: Device $dev doesn't exist"
		syntax
		return 1
	fi

	cmd=$2

	case $cmd in
	hgo)
		options=$3
		;;

	input)
		index=$3
		infmt=$4
		options=$5
		;;

	output)
		index=$3
		outfmt=$4
		options=$5
		;;

	*)
		echo "Invalid command $cmd"
		;;
	esac
}

# ------------------------------------------------------------------------------
# Execute the command
#

execute() {
	case $cmd in
	hgo)
		if [ "x$options" = xinfinite ] ; then
			$yavta -c -n 4 \
				`$mediactl -d $mdev -e "$dev hgo histo"`
		else
			$yavta -c10 -n 10 --file=histo-#.bin $options \
				`$mediactl -d $mdev -e "$dev hgo histo"`
		fi
		;;

	input)
		rpf=rpf.$index
		size=$(vsp1_entity_get_size $rpf 0)

		file=$(frame_reference $infmt $size)

		if [ "x$options" = xinfinite ] ; then
			$yavta -c -n 4 -f $infmt -s $size --file=$file $options \
				`$mediactl -d $mdev -e "$dev $rpf input"`
		else
			$yavta -c10 -n 4 -f $infmt -s $size --file=$file $options \
				`$mediactl -d $mdev -e "$dev $rpf input"`
		fi
		;;

	output)
		wpf=wpf.$index
		size=$(vsp1_entity_get_size $wpf 1)

		if [ "x$options" = xinfinite ] ; then
			$yavta -c -n 4 -f $outfmt -s $size \
				`$mediactl -d $mdev -e "$dev $wpf output"`
		else
			$yavta -c10 -n 4 -f $outfmt -s $size --skip 7 -F $options \
				`$mediactl -d $mdev -e "$dev $wpf output"`
		fi
		;;
	esac
}

parse $* && execute
