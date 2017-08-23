#!/bin/sh

#
# Test 1D histogram generation. Use a RPF -> WPF pipeline with the HGO hooked
# up at the RPF output.
#

. ./vsp-lib.sh

features="hgo rpf.0 wpf.0"
formats="RGB24 YUV444M"

test_histogram() {
	test_start "histogram in $format"

	pipe_configure rpf-hgo
	format_configure rpf-hgo $format 1024x768

	vsp_runner hgo &
	vsp_runner rpf.0 &
	vsp_runner wpf.0

	local result=$(compare_histograms)

	test_complete $result
}

test_main() {
	local format

	for format in $formats ; do
		test_histogram $format
	done
}

test_init $0 "$features"
test_run
