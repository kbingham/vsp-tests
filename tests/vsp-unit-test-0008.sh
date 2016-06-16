#!/bin/sh

#
# Test downscaling and upscaling in RGB and YUV modes with a BRU inserted in
# the pipeline, both before and after the scaler.
#

source vsp-lib.sh

features="bru rpf.0 uds wpf.0"
formats="RGB24 UYVY"

test_scale() {
	format=$1
	insize=$2
	outsize=$3
	order=$4

	if [ $order = 'after' ] ; then
		pipe=rpf-bru-uds
	else
		pipe=rpf-uds-bru
	fi

	test_start "scaling from $insize to $outsize in $format $order BRU"

	pipe_configure $pipe
	format_configure $pipe $format $insize $format $outsize

	$vsp_runner $mdev input 0 $format &
	$vsp_runner $mdev output 0 $format

	result=$(compare_frames exact)

	test_complete $result
}

test_main() {
	for format in $formats ; do
		test_scale $format 1024x768 640x480 before
		test_scale $format 640x480 1024x768 before
		test_scale $format 1024x768 640x480 after
		test_scale $format 640x480 1024x768 after
	done
}

test_init $0 "$features"
test_run
