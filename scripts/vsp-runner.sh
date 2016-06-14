#!/bin/sh

set -e

source vsp-lib.sh

genimage='./gen-image'
mediactl='media-ctl'
yavta='yavta'

# -----------------------------------------------------------------------------
# Input frame generation
#

generate_input_frame() {
	local file=$1
	local format=$2
	local size=$3

	local alpha=
	local options=

	case $format in
	ARGB555)
		alpha=255
		;;
	ABGR32 | ARGB32)
		alpha=200
		;;
	XRGB555 | XBGR32 | XRGB32)
		alpha=0
		;;
	*)
		alpha=255
		;;
	esac

	$(format_v4l2_is_yuv $format) && options="$options -y"

	$genimage -f $format -s $size -a $alpha $options -o $file \
		frames/frame-reference-1024x768.pnm
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
		file=${rpf}.bin

		generate_input_frame $file $infmt $size

		if [ "x$options" = xinfinite ] ; then
			$yavta -c -n 4 -f $infmt -s $size --file=$file $options \
				`$mediactl -d $mdev -e "$dev $rpf input"`
		else
			$yavta -c10 -n 4 -f $infmt -s $size --file=$file $options \
				`$mediactl -d $mdev -e "$dev $rpf input"`
		fi

		rm -f $file
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
