#!/bin/sh

#
# Test WPF packing in YUV mode. Use a RPF -> WPF pipeline with a fixed YUYV
# format on the input and capture output frames in all YUV formats supported
# by the WPF.
#

source vsp-lib.sh

features="rpf.0 wpf.0"
formats="NV12M NV16M NV21M NV61M UYVY VYUY YUV420M YUYV YVYU"

test_wpf_packing() {
	test_start "WPF packing in $format"

	pipe_configure rpf-wpf 0 0
	format_configure rpf-wpf 0 0 YUYV 1024x768 $format

	$vsp_runner $mdev input 0 YUYV &
	$vsp_runner $mdev output 0 $format

	result=$(compare_frames fuzzy reference)

	test_complete $result
}

test_main() {
	for format in $formats ; do
		test_wpf_packing $format
	done
}

test_init $0 "$features"
test_run
