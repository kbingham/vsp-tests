#!/bin/sh

#
# Test SRU upscaling in RGB and YUV modes.
#
# SRU processing of RGB data may generate adverse effects such as color blue.
# YUV processing is thus recommended, RGB processing must be evaluated
# carefully before putting it into practical use.
#

. ./vsp-lib.sh

features="rpf.0 sru wpf.0"
formats="RGB24 YUV444M"

test_sru() {
	local format=$1
	local insize=$2
	local outsize=$3

	test_start "SRU scaling from $insize to $outsize in $format"

	pipe_configure rpf-sru
	format_configure rpf-sru $format $insize $format $outsize

	vsp_runner rpf.0 &
	vsp_runner wpf.0

	local result=$(compare_frames)

	test_complete $result
}

test_main() {
	local format

	for format in $formats ; do
		test_sru $format 1024x768 1024x768  # without scaling
		test_sru $format 1024x768 2048x1536 # SRUx2
	done
}

test_init $0 "$features"
test_run
