#!/bin/sh

#
# Test downscaling and upscaling in RGB and YUV modes with a BRU inserted in
# the pipeline, both before and after the scaler.
#

. ./vsp-lib.sh

features="bru rpf.0 uds wpf.0"
formats="RGB24 YUV444M"

test_scale() {
	local format=$1
	local insize=$2
	local outsize=$3
	local order=$4
	local pipe

	if [ $order = 'after' ] ; then
		pipe=rpf-bru-uds
	else
		pipe=rpf-uds-bru
	fi

	test_start "scaling from $insize to $outsize in $format $order BRU"

	pipe_configure $pipe
	format_configure $pipe $format $insize $format $outsize

	vsp_runner rpf.0 &
	vsp_runner wpf.0

	local result=$(compare_frames)

	test_complete $result
}

test_main() {
	local format

	for format in $formats ; do
		test_scale $format 1024x768 640x480 before
		test_scale $format 640x480 1024x768 before
		test_scale $format 1024x768 640x480 after
		test_scale $format 640x480 1024x768 after
	done
}

test_init $0 "$features"
test_run
