#!/bin/sh

#
# Test invalid pipelines, without an RPF or without a WPF.
#

source vsp-lib.sh

features="rpf.0 wpf.0"
format=RGB24

test_no_rpf() {
	test_start "invalid pipeline with no RPF"

	pipe_configure none
	format_configure wpf $format 1024x768 0

	vsp_runner wpf.0

	# The test always passes if the kernel doesn't crash
	test_complete pass
}

test_no_wpf() {
	test_start "invalid pipeline with no WPF"

	pipe_configure none
	format_configure rpf $format 1024x768 0

	vsp_runner rpf.0

	# The test always passes if the kernel doesn't crash
	test_complete pass
}

test_main() {
	test_no_rpf
	test_no_wpf
}

test_init $0 "$features"
test_run
