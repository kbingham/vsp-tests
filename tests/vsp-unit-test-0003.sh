#!/bin/sh

#
# Test downscaling and upscaling in RGB and YUV modes. Use a RPF -> UDS -> WPF
# pipeline with identical input and output formats.
#

source vsp-lib.sh

features="rpf.0 uds wpf.0"
formats="RGB24 YUV444M"

test_scale() {
	local format=$1
	local insize=$2
	local outsize=$3

	test_start "scaling from $insize to $outsize in $format"

	pipe_configure rpf-uds
	format_configure rpf-uds $format $insize $format $outsize

	vsp_runner rpf.0 &
	vsp_runner wpf.0

	local result=$(compare_frames)

	test_complete $result
}

test_main() {
	local format

	for format in $formats ; do
		test_scale $format 640x640 640x480
		test_scale $format 1024x768 640x480
		test_scale $format 640x480 1024x768
	done
}

test_init $0 "$features"
test_run
