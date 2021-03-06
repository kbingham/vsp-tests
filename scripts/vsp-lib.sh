#!/bin/sh

genimage='./gen-image'
mediactl='media-ctl'
yavta='yavta'
frames_dir=/tmp/

# ------------------------------------------------------------------------------
# Miscellaneous
#

vsp1_device() {
	$mediactl -d $mdev -p | grep 'bus info' | sed 's/.*platform://'
}

vsp1_model() {
	$mediactl -d $mdev -p | grep 'model' | sed 's/.* //'
}

vsp1_generation() {
	echo $(vsp1_model) | sed 's/-.*//'
}

vsp1_has_feature() {
	local feature=$1
	local entity_name=$(echo $feature | sed 's/\[.*//')

	($mediactl -d $mdev -p | grep -q -- "- entity.*$entity_name") || return

	local option=$(echo $feature | cut -d '[' -f 2 -s | cut -d ']' -f 1)

	[ -z $option ] && return

	local key=$(echo $option | sed 's/:.*//')
	local value=$(echo $option | sed "s/.*:'\(.*\)'/\1/")

	case $key in
	control)
		vsp1_has_control $entity_name "$value"
		return
		;;
	*)
		return 1
		;;
	esac
}

vsp1_count_rpfs() {
	$mediactl -d $mdev -p | grep -- '- entity.*rpf.[0-9] [^i]' | wc -l
}

vsp1_count_wpfs() {
	$mediactl -d $mdev -p | grep -- '- entity.*wpf.[0-9] [^o]' | wc -l
}

__vsp1_count_brx_inputs() {
	local name=$1
	local num_pads=`$mediactl -d $mdev -p | grep "entity.*$name" | sed 's/.*(\([0-9]\) pads.*/\1/'`
	echo $((num_pads-1))
}

vsp1_count_bru_inputs() {
	__vsp1_count_brx_inputs "bru"
}

vsp1_entity_subdev() {
	$mediactl -d $mdev -e "$dev $1"
}

vsp1_entity_get_size() {
	local entity=$1
	local pad=$2

	$mediactl -d $mdev --get-v4l2 "'$dev $entity':$pad" | grep fmt | \
	      sed 's/.*\/\([0-9x]*\).*/\1/'
}

vsp1_has_control() {
	local subdev=$(vsp1_entity_subdev $1)
	local control_name=$(echo $2 | tr '+' ' ')

	$yavta --no-query -l $subdev | grep -q -- "$control_name"
}

vsp1_set_control() {
	local entity=$1
	local control_name=$(echo $2 | tr '+' ' ')
	local value=$3

	local subdev=$(vsp1_entity_subdev $entity)
	local control=$($yavta --no-query -l $subdev | grep -- "$control_name" | cut -d ' ' -f 2)

	echo "Setting control $control_name ($control) to $value" | ./logger.sh "$entity" >> $logfile
	$yavta --no-query -w "$control $value" $subdev | ./logger.sh "$entity" >> $logfile
}

vsp1_reset_controls() {
	local entity=$1
	local subdev=$(vsp1_entity_subdev $entity)

	echo "Resetting controls on $subdev" | ./logger.sh "$entity" >> $logfile
	$yavta --no-query --reset-controls $subdev | ./logger.sh "$entity" >> $logfile
}

# -----------------------------------------------------------------------------
# Reference frame generation
#

reference_frame() {
	local file=$1
	local in_format=$2
	local out_format=$3
	local size=$4
	shift 4

	local alpha=
	local options=

	# gen-image doesn't support processing HSV input images. The good news
	# is that the HSV tests that take HSV images as inputs don't need to
	# perform any processing. We can set the input format to RGB for HSB
	# reference frame generation.
	case $in_format in
	HSV24 | HSV32)
		in_format=ARGB32
		;;
	esac

	# Start with the input format to compute the alpha value being used by
	# the RPF after unpacking. Keep in sync with generate_input_frame.
	case $in_format in
	ARGB555)
		# The 1-bit alpha value is expanded to 8 bits by copying the
		# high order bits, resulting in value of 255 after unpacking.
		alpha=255
		;;
	ABGR32 | ARGB32)
		# 8-bit alpha value, hardcoded to 200.
		alpha=200
		;;
	*)
		# In all other cases the alpha value is set through a control
		# whose default value is 255.
		alpha=255
		;;
	esac

	# Convert the input alpha value based on the output format.
	case $out_format in
	ARGB555 | ABGR32 | ARGB32)
		# Pass the 8-bit alpha value unchanged to the image generator.
		;;
	XRGB555)
		# The format has the X bit hardcoded to 0.
		alpha=0
		;;
	*)
		# In all other cases the alpha value is set through a control
		# whose default value is 255.
		alpha=255
		;;
	esac

	local arg
	for arg in $* ; do
		local name=$(echo $arg | cut -d '=' -f 1)
		local value=$(echo $arg | cut -d '=' -f 2)

		case $name in
		clu)
			options="$options --clu $value"
			;;
		crop)
			options="$options --crop $value"
			;;
		hflip)
			[ x$value = x1 ] && options="$options --hflip"
			;;
		lut)
			options="$options --lut $value"
			;;
		rotate)
			[ x$value = x90 ] && {
				options="$options --rotate" ;
				__vsp_pixel_perfect=false ;
			}
			;;
		vflip)
			[ x$value = x1 ] && options="$options --vflip"
			;;
		esac
	done

	[ x$__vsp_brx_inputs != x ] && options="$options -c $__vsp_brx_inputs"

	$genimage -i $in_format -f $out_format -s $size -a $alpha $options -o $file \
		frames/frame-reference-1024x768.pnm
}

reference_histogram() {
	local file=$1
	local format=$2
	local size=$3
	local type=$4
	local hgt_hue_areas=$5

	local hue=
	[[ "x$hgt_hue_areas" != x ]] && hue="--histogram-areas $hgt_hue_areas"

	$genimage -i $format -f $format -s $size -H $file --histogram-type $type $hue \
		frames/frame-reference-1024x768.pnm
}

# ------------------------------------------------------------------------------
# Image and histogram comparison
#

#
# Compare the two frames for exact match.
#
compare_frame_exact() {
	local img_a=$3
	local img_b=$4

	local match='fail'
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
	local fmt=$(echo $1 | sed 's/M$/P/')
	local size=$2
	local img_a=$3
	local img_b=$4

	local pnm_a=${img_a/bin/pnm}
	local pnm_b=${img_b/bin/pnm}

	raw2rgbpnm -f $fmt -s $size $img_a $pnm_a > /dev/null
	raw2rgbpnm -f $fmt -s $size $img_b $pnm_b > /dev/null

	local ae=$(compare -metric ae $pnm_a $pnm_b /dev/null 2>&1)
	local mae=$(compare -metric mae $pnm_a $pnm_b /dev/null 2>&1 | sed 's/.*(\(.*\))/\1/')

	rm $pnm_a
	rm $pnm_b

	local width=$(echo $size | cut -d 'x' -f 1)
	local height=$(echo $size | cut -d 'x' -f 2)

	local ae_match=$(echo $ae $width $height | awk '{ if ($1 / $2 / $3 < 0.05) { print "pass" } else { print "fail" } }')
	local mae_match=$(echo $mae | awk '{ if ($1 < 0.01) { print "pass" } else { print "fail" } }')

	echo "Compared $img_a and $img_b: ae $ae ($ae_match) mae $mae ($mae_match)" | ./logger.sh check >> $logfile

	if [ $ae_match = 'pass' -a $mae_match = 'pass' ] ; then
		return 0
	else
		return 1
	fi
}

compare_frames() {
	local args=$*
	local pipe=$__vsp_pipe
	local in_format=$__vsp_rpf_format
	local out_format=$__vsp_wpf_format
	local wpf=$__vsp_wpf_index

	local in_fmt=$(echo $in_format | tr '[:upper:]' '[:lower:]')
	local out_fmt=$(echo $out_format | tr '[:upper:]' '[:lower:]')
	local size=$(vsp1_entity_get_size wpf.$wpf 1)

	reference_frame ${frames_dir}ref-frame.bin $in_format $out_format $size $args

	local method=exact
	local result="pass"
	local params=${args// /-}
	params=${params:+-$params}
	params=${params//\//_}
	params=${params//=/_}
	params=${params//(/_}
	params=${params//)/_}
	params=$pipe-$in_fmt-$out_fmt-$size$params

	# The system can hint when pixel-perfection is not supported,
	# however the user can override to force this requirement with
	# VSP_PIXEL_PERFECT=1 in the environment or by passing -p, or
	# --pixel-perfect on the commandline.
	if [ x$__vsp_pixel_perfect != xtrue -a x$VSP_PIXEL_PERFECT != x1 ] ; then
		method=fuzzy
	fi

	for frame in ${frames_dir}frame-*.bin ; do
		local match="true"

		(compare_frame_$method $out_format $size $frame ${frames_dir}ref-frame.bin) ||  {
			match="false" ;
			result="fail" ;
		}

		if [ $match = "false" -o x$VSP_KEEP_FRAMES = x1 ] ; then
			mv $frame ${0/.sh/}-$params-$(basename ${frame})
		fi
	done

	if [ x$VSP_KEEP_FRAMES = x1 -o $result = "fail" ] ; then
		mv ${frames_dir}ref-frame.bin ${0/.sh/}-$params-ref-frame.bin
	else
		rm -f ${frames_dir}ref-frame.bin
		rm -f ${frames_dir}frame-*.bin
	fi

	echo $result
}

compare_histogram() {
	local histo_a=$1
	local histo_b=$2

	local match='fail'
	diff -q $histo_a $histo_b > /dev/null && match='pass'

	echo "Compared $histo_a and $histo_b: $match" | ./logger.sh check >> $logfile

	if [ $match = 'pass' ] ; then
		return 0
	else
		return 1
	fi
}

compare_histograms() {
	local hgt_hue_areas=$__vsp_hgt_hue_areas
	local format=$__vsp_wpf_format
	local type=$__vsp_histo_type
	local wpf=$__vsp_wpf_index

	local fmt=$(echo $format | tr '[:upper:]' '[:lower:]')
	local size=$(vsp1_entity_get_size wpf.$wpf 1)

	reference_histogram ${frames_dir}ref-histogram.bin $format $size $type $hgt_hue_areas

	local result="pass"
	for histo in ${frames_dir}histo-*.bin ; do
		(compare_histogram $histo ${frames_dir}ref-histogram.bin) || {
			mv $histo ${0/.sh/}-$(basename ${histo/.bin/-$fmt.bin}) ;
			result="fail"
		}
	done

	if [ $result = "fail" ] ; then
		mv ${frames_dir}ref-histogram.bin ${0/.sh/}-ref-histogram-$fmt.bin
	else
		rm -f ${frames_dir}ref-histogram.bin
		rm -f ${frames_dir}histo-*.bin
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

__pipe_rpf_brx() {
	local name=$1
	local ninputs=$2

	local output=$(__vsp1_count_brx_inputs $name)

	for input in `seq 0 1 $((ninputs-1))` ; do
		$mediactl -d $mdev -l "'$dev rpf.$input':1 -> '$dev $name':$input [1]"
	done
	$mediactl -d $mdev -l "'$dev $name':$output -> '$dev wpf.0':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.0':1 -> '$dev wpf.0 output':0 [1]"

	__vsp_brx_inputs=$ninputs
}

pipe_rpf_brs() {
	__pipe_rpf_brx "brs" $*
}

pipe_rpf_bru() {
	__pipe_rpf_brx "bru" $*
}

pipe_rpf_bru_uds() {
	local bru_output=$(vsp1_count_bru_inputs)

	$mediactl -d $mdev -l "'$dev rpf.0':1 -> '$dev bru':0 [1]"
	$mediactl -d $mdev -l "'$dev bru':$bru_output -> '$dev uds.0':0 [1]"
	$mediactl -d $mdev -l "'$dev uds.0':1 -> '$dev wpf.0':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.0':1 -> '$dev wpf.0 output':0 [1]"
}

pipe_rpf_clu() {
	$mediactl -d $mdev -l "'$dev rpf.0':1 -> '$dev clu':0 [1]"
	$mediactl -d $mdev -l "'$dev clu':1 -> '$dev wpf.0':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.0':1 -> '$dev wpf.0 output':0 [1]"
}

pipe_rpf_hgo() {
	$mediactl -d $mdev -l "'$dev rpf.0':1 -> '$dev wpf.0':0 [1]"
	$mediactl -d $mdev -l "'$dev rpf.0':1 -> '$dev hgo':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.0':1 -> '$dev wpf.0 output':0 [1]"
}

pipe_rpf_hst() {
	$mediactl -d $mdev -l "'$dev rpf.0':1 -> '$dev hst':0 [1]"
	$mediactl -d $mdev -l "'$dev hst':1 -> '$dev wpf.0':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.0':1 -> '$dev wpf.0 output':0 [1]"
}

pipe_rpf_hgt() {
	$mediactl -d $mdev -l "'$dev rpf.0':1 -> '$dev hst':0 [1]"
	$mediactl -d $mdev -l "'$dev hst':1 -> '$dev hgt':0 [1]"
	$mediactl -d $mdev -l "'$dev hst':1 -> '$dev hsi':0 [1]"
	$mediactl -d $mdev -l "'$dev hsi':1 -> '$dev wpf.0':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.0':1 -> '$dev wpf.0 output':0 [1]"

	__vsp_wpf_index=0
}

pipe_rpf_lut() {
	$mediactl -d $mdev -l "'$dev rpf.0':1 -> '$dev lut':0 [1]"
	$mediactl -d $mdev -l "'$dev lut':1 -> '$dev wpf.0':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.0':1 -> '$dev wpf.0 output':0 [1]"
}

pipe_rpf_uds() {
	$mediactl -d $mdev -l "'$dev rpf.0':1 -> '$dev uds.0':0 [1]"
	$mediactl -d $mdev -l "'$dev uds.0':1 -> '$dev wpf.0':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.0':1 -> '$dev wpf.0 output':0 [1]"
}

pipe_rpf_uds_bru() {
	local bru_output=$(vsp1_count_bru_inputs)

	$mediactl -d $mdev -l "'$dev rpf.0':1 -> '$dev uds.0':0 [1]"
	$mediactl -d $mdev -l "'$dev uds.0':1 -> '$dev bru':0 [1]"
	$mediactl -d $mdev -l "'$dev bru':$bru_output -> '$dev wpf.0':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.0':1 -> '$dev wpf.0 output':0 [1]"
}

pipe_rpf_uds_sru() {
	$mediactl -d $mdev -l "'$dev rpf.0':1 -> '$dev uds.0':0 [1]"
	$mediactl -d $mdev -l "'$dev uds.0':1 -> '$dev sru':0 [1]"
	$mediactl -d $mdev -l "'$dev sru':1 -> '$dev wpf.0':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.0':1 -> '$dev wpf.0 output':0 [1]"
}

pipe_rpf_sru() {
	$mediactl -d $mdev -l "'$dev rpf.0':1 -> '$dev sru':0 [1]"
	$mediactl -d $mdev -l "'$dev sru':1 -> '$dev wpf.0':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.0':1 -> '$dev wpf.0 output':0 [1]"
}

pipe_rpf_sru_uds() {
	$mediactl -d $mdev -l "'$dev rpf.0':1 -> '$dev sru':0 [1]"
	$mediactl -d $mdev -l "'$dev sru':1 -> '$dev uds.0':0 [1]"
	$mediactl -d $mdev -l "'$dev uds.0':1 -> '$dev wpf.0':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.0':1 -> '$dev wpf.0 output':0 [1]"
}

pipe_rpf_wpf() {
	local rpf=$1
	local wpf=$2

	$mediactl -d $mdev -l "'$dev rpf.$rpf':1 -> '$dev wpf.$wpf':0 [1]"
	$mediactl -d $mdev -l "'$dev wpf.$wpf':1 -> '$dev wpf.$wpf output':0 [1]"

	__vsp_wpf_index=$wpf
}

pipe_reset() {
	$mediactl -d $mdev -r

	__vsp_brx_inputs=
	__vsp_histo_type=
	__vsp_rpf_format=
	__vsp_wpf_index=0
	__vsp_wpf_format=
	__vsp_pixel_perfect=true
}

pipe_configure() {
	local pipe=${1//-/_}
	shift 1

	pipe_reset
	pipe_$pipe $*

	__vsp_pipe=$pipe
}

# ------------------------------------------------------------------------------
# Format Configuration
#

format_v4l2_to_mbus() {
	case $1 in
	RGB332 | ARGB555 | XRGB555 | RGB565 | BGR24 | RGB24 | XBGR32 | XRGB32 | ABGR32 | ARGB32)
		echo "ARGB32";
		;;

	HSV24 | HSV32)
		echo "AHSV8888_1X32";
		;;

	UYVY | VYUY | YUYV | YVYU | NV12M | NV16M | NV21M | NV61M | YUV420M | YUV422M | YUV444M | YVU420M | YVU422M | YVU444M)
		echo "AYUV32"
		;;

	*)
		echo "Invalid format $1" >&2
		echo -e "Valid formats are
\tRGB332, ARGB555, XRGB555, RGB565, BGR24, RGB24,
\tXBGR32, XRGB32, ABGR32, ARGB32, HSV24, HSV32
\tUYVY, VYUY, YUYV, YVYU,
\tNV12M, NV16M, NV21M, NV61M,
\tYUV420M, YUV422M, YUV444M,
\tYVU420M, YVU422M, YVU444M" >&2
		exit 1
	esac
}

format_v4l2_is_yuv() {
	local format=$(format_v4l2_to_mbus $1)
	[ $format = 'AYUV32' ]
}

format_rpf() {
	local format=$(format_v4l2_to_mbus $1)
	local size=$2
	local rpf=$3

	$mediactl -d $mdev -V "'$dev rpf.$rpf':0 [fmt:$format/$size]"

	__vsp_rpf_format=$1
}

__format_rpf_brx() {
	local name=$1
	local format=$(format_v4l2_to_mbus $2)
	local size=$3
	local ninputs=$4
	local offset=0

	local output=$(__vsp1_count_brx_inputs $name)

	for input in `seq 0 1 $((ninputs-1))` ; do
		offset=$((offset+50))
		$mediactl -d $mdev -V "'$dev rpf.$input':0 [fmt:$format/$size]"
		$mediactl -d $mdev -V "'$dev $name':$input [fmt:$format/$size compose:($offset,$offset)/$size]"
	done

	$mediactl -d $mdev -V "'$dev $name':$output [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev wpf.0':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev wpf.0':1 [fmt:$format/$size]"

	__vsp_rpf_format=$2
	__vsp_wpf_format=$2
}

format_rpf_brs() {
	__format_rpf_brx "brs" $*
}

format_rpf_bru() {
	__format_rpf_brx "bru" $*
}

format_rpf_bru_uds() {
	local infmt=$(format_v4l2_to_mbus $1)
	local insize=$2
	local outfmt=$(format_v4l2_to_mbus $3)
	local outsize=$4

	local bru_output=$(vsp1_count_bru_inputs)

	$mediactl -d $mdev -V "'$dev rpf.0':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev bru':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev bru':$bru_output [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev uds.0':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev uds.0':1 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':0 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':1 [fmt:$outfmt/$outsize]"

	[ $insize != $outsize ] && __vsp_pixel_perfect=false
	__vsp_rpf_format=$1
	__vsp_wpf_format=$3
}

format_rpf_clu() {
	local format=$(format_v4l2_to_mbus $1)
	local size=$2

	$mediactl -d $mdev -V "'$dev rpf.0':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev clu':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev clu':1 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev wpf.0':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev wpf.0':1 [fmt:$format/$size]"

	__vsp_rpf_format=$1
	__vsp_wpf_format=$1
}

format_rpf_hst() {
	local format=$(format_v4l2_to_mbus $1)
	local size=$2

	$mediactl -d $mdev -V "'$dev rpf.0':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev hst':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev hst':1 [fmt:AHSV8888_1X32/$size]"
	$mediactl -d $mdev -V "'$dev wpf.0':0 [fmt:AHSV8888_1X32/$size]"
	$mediactl -d $mdev -V "'$dev wpf.0':1 [fmt:AHSV8888_1X32/$size]"

	__vsp_rpf_format=$1
	__vsp_wpf_format=$3
}

format_rpf_hgo() {
	local format=$(format_v4l2_to_mbus $1)
	local size=$2
	local crop=${3:+crop:$3}
	local compose=${4:+compose:$4}

	$mediactl -d $mdev -V "'$dev rpf.0':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev wpf.0':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev wpf.0':1 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev hgo':0   [fmt:$format/$size $crop $compose]"

	__vsp_histo_type=hgo
	__vsp_rpf_format=$1
	__vsp_wpf_format=$1
}

format_rpf_hgt() {
	local format=$(format_v4l2_to_mbus $1)
	local size=$2
	local crop=${3:+crop:$3}
	local compose=${4:+compose:$4}

	$mediactl -d $mdev -V "'$dev rpf.0':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev hst':0   [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev hgt':0   [fmt:$format/$size $crop $compose]"
	$mediactl -d $mdev -V "'$dev hsi':0   [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev wpf.0':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev wpf.0':1 [fmt:$format/$size]"

	__vsp_histo_type=hgt
	__vsp_rpf_format=$1
	__vsp_wpf_format=$1
}

format_rpf_lut() {
	local format=$(format_v4l2_to_mbus $1)
	local size=$2

	$mediactl -d $mdev -V "'$dev rpf.0':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev lut':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev lut':1 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev wpf.0':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev wpf.0':1 [fmt:$format/$size]"

	__vsp_rpf_format=$1
	__vsp_wpf_format=$1
}

format_rpf_uds() {
	local infmt=$(format_v4l2_to_mbus $1)
	local insize=$2
	local outfmt=$(format_v4l2_to_mbus $3)
	local outsize=$4

	$mediactl -d $mdev -V "'$dev rpf.0':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev uds.0':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev uds.0':1 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':0 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':1 [fmt:$outfmt/$outsize]"

	[ $insize != $outsize ] && __vsp_pixel_perfect=false
	__vsp_rpf_format=$1
	__vsp_wpf_format=$3
}

format_rpf_uds_bru() {
	local infmt=$(format_v4l2_to_mbus $1)
	local insize=$2
	local outfmt=$(format_v4l2_to_mbus $3)
	local outsize=$4

	local bru_output=$(vsp1_count_bru_inputs)

	$mediactl -d $mdev -V "'$dev rpf.0':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev uds.0':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev uds.0':1 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev bru':0 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev bru':$bru_output [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':0 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':1 [fmt:$outfmt/$outsize]"

	[ $insize != $outsize ] && __vsp_pixel_perfect=false
	__vsp_rpf_format=$1
	__vsp_wpf_format=$3
}

format_rpf_uds_sru() {
	local infmt=$(format_v4l2_to_mbus $1)
	local insize=$2
	local midsize=$3
	local outfmt=$(format_v4l2_to_mbus $4)
	local outsize=$5

	$mediactl -d $mdev -V "'$dev rpf.0':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev uds.0':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev uds.0':1 [fmt:$infmt/$midsize]"
	$mediactl -d $mdev -V "'$dev sru':0 [fmt:$infmt/$midsize]"
	$mediactl -d $mdev -V "'$dev sru':1 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':0 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':1 [fmt:$outfmt/$outsize]"

	__vsp_pixel_perfect=false
	__vsp_rpf_format=$1
	__vsp_wpf_format=$4
}

format_rpf_sru() {
	local infmt=$(format_v4l2_to_mbus $1)
	local insize=$2
	local outfmt=$(format_v4l2_to_mbus $3)
	local outsize=$4

	$mediactl -d $mdev -V "'$dev rpf.0':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev sru':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev sru':1 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':0 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':1 [fmt:$outfmt/$outsize]"

	__vsp_pixel_perfect=false
	__vsp_rpf_format=$1
	__vsp_wpf_format=$3
}

format_rpf_sru_uds() {
	local infmt=$(format_v4l2_to_mbus $1)
	local insize=$2
	local midsize=$3
	local outfmt=$(format_v4l2_to_mbus $4)
	local outsize=$5

	$mediactl -d $mdev -V "'$dev rpf.0':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev sru':0 [fmt:$infmt/$insize]"
	$mediactl -d $mdev -V "'$dev sru':1 [fmt:$infmt/$midsize]"
	$mediactl -d $mdev -V "'$dev uds.0':0 [fmt:$infmt/$midsize]"
	$mediactl -d $mdev -V "'$dev uds.0':1 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':0 [fmt:$infmt/$outsize]"
	$mediactl -d $mdev -V "'$dev wpf.0':1 [fmt:$outfmt/$outsize]"

	__vsp_pixel_perfect=false
	__vsp_rpf_format=$1
	__vsp_wpf_format=$4
}

format_rpf_wpf() {
	local rpf=$1
	local wpf=$2
	local infmt=$(format_v4l2_to_mbus $3)
	local size=$4
	local outfmt=$(format_v4l2_to_mbus $5)
	local rpfcrop=
	local wpfcrop=
	local rpfoutsize=
	local outsize=
	local option=

	__vsp_rpf_format=$3
	__vsp_wpf_format=$5

	shift 5

	for option in $* ; do
		case $option in
		--rpfcrop=*)
			rpfcrop=${option/--rpfcrop=/}
			;;

		--wpfcrop=*)
			wpfcrop=${option/--wpfcrop=/}
			;;
		*)
			echo "format_rpf_wpf: Unrecognised argument $option"
			return 1
			;;
		esac
	done

	if [ x$rpfcrop != 'x' ] ; then
		rpfcrop="crop:$rpfcrop"
		rpfoutsize=$(echo $rpfcrop | sed 's/.*\///')
	else
		rpfoutsize=$size
	fi

	if [ x$wpfcrop != 'x' ] ; then
		wpfcrop="crop:$wpfcrop"
		outsize=$(echo $wpfcrop | sed 's/.*\///')
	else
		outsize=$rpfoutsize
	fi

	$mediactl -d $mdev -V "'$dev rpf.$rpf':0 [fmt:$infmt/$size $rpfcrop]"
	$mediactl -d $mdev -V "'$dev rpf.$rpf':1 [fmt:$infmt/$rpfoutsize]"
	$mediactl -d $mdev -V "'$dev wpf.$wpf':0 [fmt:$infmt/$rpfoutsize $wpfcrop]"
	$mediactl -d $mdev -V "'$dev wpf.$wpf':1 [fmt:$outfmt/$outsize]"
}

format_wpf() {
	local format=$(format_v4l2_to_mbus $1)
	local size=$2
	local wpf=$3

	$mediactl -d $mdev -V "'$dev wpf.$wpf':0 [fmt:$format/$size]"
	$mediactl -d $mdev -V "'$dev wpf.$wpf':1 [fmt:$format/$size]"

	__vsp_wpf_format=$1
}

format_configure() {
	local pipe=${1//-/_}
	shift 1

	format_$pipe $*
}

# ------------------------------------------------------------------------------
# Module-specific configuration
#

hgt_configure() {
	local hue_areas=$1

	vsp1_set_control hgt 'Boundary Values for Hue Area' "{$hue_areas}"

	__vsp_hgt_hue_areas=$hue_areas
}

# ------------------------------------------------------------------------------
# Frame capture and output
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

	$(format_v4l2_is_yuv $format) && options="$options -C -i YUV444M"

	$genimage -f $format -s $size -a $alpha $options -o $file \
		frames/frame-reference-1024x768.pnm
}

vsp_runner() {
	local entity=$1
	shift

	local option
	local buffers=4
	local count=10
	local pause=
	local skip=7
	local queue_late=

	for option in $* ; do
		case $option in
		--buffers=*)
			buffers=${option/--buffers=/}
			;;

		--count=*)
			count=${option/--count=/}
			;;

		--pause=*)
			pause=${option/--pause=/}
			;;

		--skip=*)
			skip=${option/--skip=/}
			;;
		--queue-late)
			queue_late=queue-late
			;;
		*)
			return 1
			;;
		esac
	done

	local file
	local videodev
	local format
	local size

	case $entity in
	hgo)
		videodev=$(vsp1_entity_subdev "hgo histo")
		file="${frames_dir}histo-#.bin"
		buffers=10
		;;

	hgt)
		videodev=$(vsp1_entity_subdev "hgt histo")
		file="${frames_dir}histo-#.bin"
		buffers=10
		;;

	rpf.*)
		videodev=$(vsp1_entity_subdev "$entity input")
		format=$__vsp_rpf_format
		size=$(vsp1_entity_get_size $entity 0)
		file=${frames_dir}${entity}.bin
		generate_input_frame $file $format $size
		;;

	wpf.*)
		videodev=$(vsp1_entity_subdev "$entity output")
		format=$__vsp_wpf_format
		size=$(vsp1_entity_get_size $entity 1)
		file="${frames_dir}frame-#.bin"
		;;
	esac

	$yavta -c$count -n $buffers ${format:+-f $format} ${size:+-s $size} \
		${skip:+--skip $skip} ${file:+--file=$file} ${pause:+-p$pause} \
		${queue_late:+--$queue_late} \
		$videodev | ./logger.sh $entity >> $logfile
}

vsp_runner_find() {
	local entity=$1
	local videodev

	case $entity in
	hgo)
		videodev=$(vsp1_entity_subdev "hgo histo")
		;;

	hgt)
		videodev=$(vsp1_entity_subdev "hgt histo")
		;;

	rpf.*)
		videodev=$(vsp1_entity_subdev "$entity input")
		;;

	wpf.*)
		videodev=$(vsp1_entity_subdev "$entity output")
		;;
	esac

	local pid

	for pid in $(pidof yavta) ; do
		(ls -l /proc/$pid/fd/ | grep -q "$videodev$") && {
			echo $pid ;
			break
		}
	done
}

vsp_runner_wait() {
	local timeout=5
	local pid

	while [ $timeout != 0 ] ; do
		pid=$(vsp_runner_find $1)
		[ x$pid != x ] && break
		sleep 1
		timeout=$((timeout-1))
	done

	[ x$pid != x ] || return

	while [ ! -f .yavta.wait.$pid ] ; do
		sleep 1
	done
}

vsp_runner_resume() {
	local pid=$(vsp_runner_find $1)

	[ x$pid != x ] && kill -USR1 $pid
}

# ------------------------------------------------------------------------------
# Test run
#

test_init() {
	export logfile=${1/sh/log}
	local features=$2
	local optional_features=$3

	rm -f $logfile
	rm -f ${1/.sh/}*.bin

	local best_features_count=0
	local best_mdev=

	for mdev in /dev/media* ; do
		dev=$(vsp1_device $mdev)

		local match='true'
		for feature in $features ; do
			$(vsp1_has_feature "$feature") || {
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

		local features_count=0
		for feature in $optional_features ; do
			$(vsp1_has_feature "$feature") && {
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
		echo "No device found with feature set \`$features'" | ./logger.sh config >> $logfile
		echo "Test requires unavailable feature set \`$features': skipped" >&2
		exit 1
	fi

	mdev=$best_mdev
	dev=$(vsp1_device $mdev)
	echo "Using device $mdev ($dev)" | ./logger.sh config >> $logfile

	# Reset any rotation or flipping controls
	vsp1_reset_controls wpf.0

	vsp_runner=./vsp-runner.sh
}

test_start() {
	echo "Testing $1" | ./logger.sh >> $logfile
	echo -n "Testing $1: " >&2
}

test_complete() {
	echo "Done: $1" | ./logger.sh >> $logfile
	echo $1 >&2

	rm -f ${frames_dir}frame-*.bin
	rm -f ${frames_dir}histo-*.bin
	rm -f ${frames_dir}rpf.*.bin
}

test_run() {
	test_main | ./logger.sh error >> $logfile
}

# ------------------------------------------------------------------------------
# Common argument parsing
#
# non-recognised arguments are restored, to allow tests to implement their own
# parsing if necessary.

POSITIONAL=()
while [[ $# -gt 0 ]]
do
case $1 in
	-x|--debug)
		set -x;
		shift
		;;
	-k|--keep-frames)
		export VSP_KEEP_FRAMES=1
		shift
		;;
	-p|--pixel-perfect)
		export VSP_PIXEL_PERFECT=1
		shift
		;;
	-h|--help)
		echo "$(basename $0): VSP Test library"
		echo "  -x|--debug          enable shell debug"
		echo "  -k|--keep-frames    keep generated and captured frames"
		echo "  -p|--pixel-perfect  frames must match with pixel perfection"
		echo "  -h|--help           this help"
		exit
		shift
		;;
	*)    # unknown option
		POSITIONAL+=("$1") # save it in an array for later
		shift # past argument
		;;
esac
done
set -- "${POSITIONAL[@]}" # restore positional parameters
