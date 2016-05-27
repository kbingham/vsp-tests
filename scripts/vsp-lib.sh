#!/bin/sh

genimage='./gen-image'
mediactl='media-ctl'
yavta='yavta'

# ------------------------------------------------------------------------------
# Miscellaneous
#

vsp1_device() {
	$mediactl -d $mdev -p | grep 'bus info' | sed 's/.*platform://'
}

vsp1_has_feature() {
	feature=$1

	$mediactl -d $mdev -p | grep -q -- "- entity.*$feature"
}

vsp1_count_rpfs() {
	$mediactl -d $mdev -p | grep -- '- entity.*rpf.[0-9] [^i]' | wc -l
}

vsp1_count_wpfs() {
	$mediactl -d $mdev -p | grep -- '- entity.*wpf.[0-9] [^o]' | wc -l
}

vsp1_count_bru_inputs() {
	num_pads=`media-ctl -p | grep 'entity.*bru' | sed 's/.*(\([0-9]\) pads.*/\1/'`
	echo $((num_pads-1))
}

vsp1_entity_subdev() {
	$mediactl -d $mdev -e "$dev $1"
}

vsp1_entity_get_size() {
	entity=$1
	pad=$2

	$mediactl -d $mdev --get-v4l2 "'$dev $entity':$pad" | grep fmt | \
	      sed 's/.*\/\([0-9x]*\).*/\1/'
}

# -----------------------------------------------------------------------------
# Referance frame generation
#

reference_frame() {
	local file=$1
	local reftype=$2
	local format=$3
	local size=$4

	local alpha=
	local options=

	case $reftype in
	reference | scaled)
		;;
	composed-*)
		options="$options -c ${reftype/composed-/}"
		;;
	esac

	case $format in
	ARGB555)
		alpha=255
		;;
	ABGR32 | ARGB32)
		alpha=200
		;;
	XRGB555)
		# XRGB555 has the X bit hardcoded to 0
		alpha=0
		;;
	XBGR32 | XRGB32)
		# The X bits are configurable with a default value of 255
		alpha=255
		;;
	*)
		alpha=255
		;;
	esac

	$(format_v4l2_is_yuv $format) && options="$options -y"

	$genimage -f $format -s $size -a $alpha $options -o $file \
		frames/frame-reference-1024x768.pnm
}

reference_histogram() {
	local file=$1
	local format=$2
	local size=$3

	local yuv=
	$(format_v4l2_is_yuv $format) && yuv="-y"

	$genimage -f $format $yuv -s $size -H $file \
		frames/frame-reference-1024x768.pnm
}

# ------------------------------------------------------------------------------
# Image and histogram comparison
#

#
# Compare the two frames for exact match.
#
compare_frame_exact() {
	img_a=$3
	img_b=$4

	match='fail'
	diff -q $img_a $img_b > /dev/null && match='pass'

	echo "Compared $img_a and $img_b: $match" | ./logger.sh check >> $logfile

	if [ $match = 'pass' ] ; then
		return 0
	else
		return 1
	fi
}

#
# Compare the two frames using a fuzzy match algorithm to account for errors
# introduced by the YUV packing. Accept a maximum 1% mean average error over
# the whole frame with no more than 5% of the pixels differing.
#
compare_frame_fuzzy() {
	fmt=$(echo $1 | sed 's/M$/P/')
	size=$2
	img_a=$3
	img_b=$4

	pnm_a=${img_a/bin/pnm}
	pnm_b=${img_b/bin/pnm}

	raw2rgbpnm -f $fmt -s $size $img_a $pnm_a > /dev/null
	raw2rgbpnm -f $fmt -s $size $img_b $pnm_b > /dev/null

	ae=$(compare -metric ae $pnm_a $pnm_b /dev/null 2>&1)
	mae=$(compare -metric mae $pnm_a $pnm_b /dev/null 2>&1 | sed 's/.*(\(.*\))/\1/')

	rm $pnm_a
	rm $pnm_b

	width=$(echo $size | cut -d 'x' -f 1)
	height=$(echo $size | cut -d 'x' -f 2)

	ae_match=$(echo $ae $width $height | awk '{ if ($1 / $2 / $3 < 0.05) { print "pass" } else { print "fail" } }')
	mae_match=$(echo $mae | awk '{ if ($1 < 0.01) { print "pass" } else { print "fail" } }')

	echo "Compared $img_a and $img_b: ae $ae ($ae_match) mae $mae ($mae_match)" | ./logger.sh check >> $logfile

	if [ $ae_match = 'pass' -a $mae_match = 'pass' ] ; then
		return 0
	else
		return 1
	fi
}

compare_frames() {
	method=$1
	reftype=$2
	format=$3
	wpf=$4

	fmt=$(echo $format | tr '[:upper:]' '[:lower:]')
	size=$(vsp1_entity_get_size wpf.$wpf 1)

	reference_frame ref-frame.bin $reftype $format $size

	result="pass"
	for frame in frame-*.bin ; do
		(compare_frame_$method $format $size $frame ref-frame.bin) || {
			mv $frame ${0/.sh/}-${frame/.bin/-$fmt-$size.bin} ;
			result="fail"
		}
	done

	if [ $result = "fail" ] ; then
		mv ref-frame.bin ${0/.sh/}-ref-frame-$fmt-$size.bin
	else
		rm -f ref-frame.bin
	fi

	echo $result
}

compare_histogram() {
	histo_a=$1
	histo_b=$2

	match='fail'
	diff -q $histo_a $histo_b > /dev/null && match='pass'

	echo "Compared $histo_a and $histo_b: $match" | ./logger.sh check >> $logfile

	if [ $match = 'pass' ] ; then
		return 0
	else
		return 1
	fi
}

compare_histograms() {
	format=$1
	wpf=$2

	fmt=$(echo $format | tr '[:upper:]' '[:lower:]')
	size=$(vsp1_entity_get_size wpf.$wpf 1)

	reference_histogram ref-histogram.bin $format $size

	result="pass"
	for histo in histo-*.bin ; do
		(compare_histogram $histo ref-histogram.bin) || {
			mv $histo ${0/.sh/}-${histo/.bin/-$fmt.bin} ;
			result="fail"
		}
	done

	if [ $result = "fail" ] ; then
		mv ref-histogram.bin ${0/.sh/}-ref-histogram-$fmt.bin
	else
		rm -f ref-histogram.bin
	fi

	echo $result
}

# ------------------------------------------------------------------------------
# Pipeline configuration
#

pipe_none() {
	# Nothing to be done
	return
}

pipe_rpf_bru() {
	ninputs=$1

	bru_output=$(vsp1_count_bru_inputs)

	for input in `seq 0 1 $((ninputs-1))` ; do
		$mediactl -d $mdev -l "'$dev rpf.$input':1 -> '$dev bru':$input [1]"
	done
	$mediactl -d $mdev -l "'$dev bru':$bru_output -> '$dev wpf.0':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.0':1 -> '$dev wpf.0 output':0 [1]"
}

pipe_rpf_bru_uds() {
	bru_output=$(vsp1_count_bru_inputs)

	$mediactl -d $mdev -l "'$dev rpf.0':1 -> '$dev bru':0 [1]"
	$mediactl -d $mdev -l "'$dev bru':$bru_output -> '$dev uds.0':0 [1]"
	$mediactl -d $mdev -l "'$dev uds.0':1 -> '$dev wpf.0':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.0':1 -> '$dev wpf.0 output':0 [1]"
}

pipe_rpf_hgo() {
	$mediactl -d $mdev -l "'$dev rpf.0':1 -> '$dev wpf.0':0 [1]"
	$mediactl -d $mdev -l "'$dev rpf.0':1 -> '$dev hgo':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.0':1 -> '$dev wpf.0 output':0 [1]"
}

pipe_rpf_uds() {
	$mediactl -d $mdev -l "'$dev rpf.0':1 -> '$dev uds.0':0 [1]"
	$mediactl -d $mdev -l "'$dev uds.0':1 -> '$dev wpf.0':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.0':1 -> '$dev wpf.0 output':0 [1]"
}

pipe_rpf_uds_bru() {
	bru_output=$(vsp1_count_bru_inputs)

	$mediactl -d $mdev -l "'$dev rpf.0':1 -> '$dev uds.0':0 [1]"
	$mediactl -d $mdev -l "'$dev uds.0':1 -> '$dev bru':0 [1]"
	$mediactl -d $mdev -l "'$dev bru':$bru_output -> '$dev wpf.0':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.0':1 -> '$dev wpf.0 output':0 [1]"
}

pipe_rpf_wpf() {
	rpf=$1
	wpf=$2

	$mediactl -d $mdev -l "'$dev rpf.$rpf':1 -> '$dev wpf.$wpf':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.$wpf':1 -> '$dev wpf.$wpf output':0 [1]"
}

pipe_reset() {
	$mediactl -d $mdev -r
}

pipe_configure() {
	pipe=${1//-/_}
	shift 1

	pipe_reset
	pipe_$pipe $*
}

# ------------------------------------------------------------------------------
# Format Configuration
#

format_v4l2_to_mbus() {
	case $1 in
	RGB332 | ARGB555 | XRGB555 | RGB565 | BGR24 | RGB24 | XBGR32 | XRGB32 | ABGR32 | ARGB32)
		echo "ARGB32";
		;;

	UYVY | VYUY | YUYV | YVYU | NV12M | NV16M | NV21M | NV61M | YUV420M | YUV422M | YUV444M)
		echo "AYUV32"
		;;

	*)
		echo "Invalid format $1" >&2
		echo -e "Valid formats are
\tRGB332, ARGB555, XRGB555, RGB565, BGR24, RGB24,
\tXBGR32, XRGB32, ABGR32, ARGB32,
\tUYVY, VYUY, YUYV, YVYU,
\tNV12M, NV16M, NV21M, NV61M,
\tYUV420M, YUV422M, YUV444M" >&2
		exit 1
	esac
}

format_v4l2_is_yuv() {
	local format=$(format_v4l2_to_mbus $1)
	[ $format = 'AYUV32' ]
}

format_rpf() {
	format=$(format_v4l2_to_mbus $1)
	size=$2
	rpf=$3

	$mediactl -d $mdev -V "'$dev rpf.$rpf':0 [fmt:$format/$size]"
}

format_rpf_bru() {
	format=$(format_v4l2_to_mbus $1)
	size=$2
	ninputs=$3
	offset=0

	bru_output=$(vsp1_count_bru_inputs)

	for input in `seq 0 1 $((ninputs-1))` ; do
		offset=$((offset+50))
		$mediactl -d $mdev -V "'$dev rpf.$input':0 [fmt:$format/$size]"
		$mediactl -d $mdev -V "'$dev bru':$input   [fmt:$format/$size compose:($offset,$offset)/$size]"
	done

	$mediactl -d $mdev -V "'$dev bru':$bru_output [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev wpf.0':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev wpf.0':1 [fmt:$format/$size]"
}

format_rpf_bru_uds() {
	infmt=$(format_v4l2_to_mbus $1)
	insize=$2
	outfmt=$(format_v4l2_to_mbus $3)
	outsize=$4

	bru_output=$(vsp1_count_bru_inputs)

	$mediactl -d $mdev -V "'$dev rpf.0':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev bru':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev bru':$bru_output [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev uds.0':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev uds.0':1 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':0 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':1 [fmt:$outfmt/$outsize]"
}

format_rpf_hgo() {
	format=$(format_v4l2_to_mbus $1)
	size=$2
	crop=${3:+crop:$3}
	compose=${4:+compose:$4}

	$mediactl -d $mdev -V "'$dev rpf.0':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev wpf.0':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev wpf.0':1 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev hgo':0   [fmt:$format/$size $crop $compose]"
}

format_rpf_uds() {
	infmt=$(format_v4l2_to_mbus $1)
	insize=$2
	outfmt=$(format_v4l2_to_mbus $3)
	outsize=$4

	$mediactl -d $mdev -V "'$dev rpf.0':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev uds.0':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev uds.0':1 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':0 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':1 [fmt:$outfmt/$outsize]"
}

format_rpf_uds_bru() {
	infmt=$(format_v4l2_to_mbus $1)
	insize=$2
	outfmt=$(format_v4l2_to_mbus $3)
	outsize=$4

	bru_output=$(vsp1_count_bru_inputs)

	$mediactl -d $mdev -V "'$dev rpf.0':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev uds.0':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev uds.0':1 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev bru':0 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev bru':$bru_output [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':0 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':1 [fmt:$outfmt/$outsize]"
}

format_rpf_wpf() {
	rpf=$1
	wpf=$2
	infmt=$(format_v4l2_to_mbus $3)
	size=$4
	outfmt=$(format_v4l2_to_mbus $5)
	crop=$6

	if [ x$crop != 'x' ] ; then
		crop="crop:$crop"
		outsize=$(echo $crop | sed 's/.*\///')
	else
		outsize=$size
	fi

	$mediactl -d $mdev -V "'$dev rpf.$rpf':0 [fmt:$infmt/$size]"
	$mediactl -d $mdev -V "'$dev wpf.$wpf':0 [fmt:$infmt/$size $crop]"
	$mediactl -d $mdev -V "'$dev wpf.$wpf':1 [fmt:$outfmt/$outsize]"
}

format_wpf() {
	format=$(format_v4l2_to_mbus $1)
	size=$2
	wpf=$3

	$mediactl -d $mdev -V "'$dev wpf.$wpf':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev wpf.$wpf':1 [fmt:$format/$size]"
}

format_configure() {
	pipe=${1//-/_}
	shift 1

	format_$pipe $*
}

# ------------------------------------------------------------------------------
# Test run
#

test_init() {
	logfile=${1/sh/log}
	features=$2
	optional_features=$3

	rm -f $logfile
	rm -f *.bin

	best_features_count=0

	for mdev in /dev/media* ; do
		match='true'
		for feature in $features ; do
			$(vsp1_has_feature $feature) || {
				match='false';
				break;
			}
		done

		if [ $match == 'false' ] ; then
			continue
		fi

		if [ -z "$optional_features" ] ; then
			best_mdev=$mdev
			break
		fi

		features_count=0
		for feature in $optional_features ; do
			$(vsp1_has_feature $feature) && {
				features_count=$((features_count+1))
				match='false';
				break;
			}
		done

		if [ $features_count -ge $best_features_count ] ; then
			best_mdev=$mdev
			best_features_count=$features_count
		fi
	done

	if [ -z $best_mdev ] ; then
		echo "No device found with feature set $features" | ./logger.sh config >> $logfile
		exit 1
	fi

	mdev=$best_mdev
	dev=$(vsp1_device $best_mdev)
	echo "Using device $mdev ($dev)" | ./logger.sh config >> $logfile

	vsp_runner=./vsp-runner.sh
}

test_start() {
	echo "Testing $1" | ./logger.sh >> $logfile
	echo -n "Testing $1: " >&2
}

test_complete() {
	echo "Done: $1" | ./logger.sh >> $logfile
	echo $1 >&2

	rm -f frame-*.bin
	rm -f histo-*.bin
}
