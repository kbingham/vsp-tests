#!/bin/sh

#
# Test composition through the BRU in RGB and YUV formats.
#

source vsp-lib.sh

features="rpf.0 rpf.1 bru wpf.0"
formats="RGB24 YUV444M"

test_bru() {
	format=$1
	ninputs=$2

	test_start "BRU in $format with $ninputs inputs"

	pipe_configure rpf-bru $ninputs
	format_configure rpf-bru $format 1024x768 $ninputs

	for input in `seq 0 1 $((ninputs-1))` ; do
		vsp_runner rpf.$input &
	done
	vsp_runner wpf.0

	result=$(compare_frames)

	test_complete $result
}

test_main() {
	max_inputs=$(vsp1_count_bru_inputs)

	for format in $formats ; do
		for ninputs in `seq $max_inputs` ; do
			test_bru $format $ninputs
		done
	done
}

test_init $0 "$features"
test_run
