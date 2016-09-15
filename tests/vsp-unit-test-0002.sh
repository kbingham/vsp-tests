#!/bin/sh

#
# Test WPF packing in YUV mode. Use a RPF -> WPF pipeline with a fixed YUYV
# format on the input and capture output frames in all YUV formats supported
# by the WPF.
#

source vsp-lib.sh

features="rpf.0 wpf.0"
formats="NV12M NV16M NV21M NV61M UYVY VYUY YUV420M YUV422M YUV444M YUYV YVYU"

test_wpf_packing() {
	test_start "WPF packing in $format"

	if [ $format = VYUY -a $(vsp1_generation) != VSP1 ] ; then
		test_complete skip
		return
	fi

	pipe_configure rpf-wpf 0 0
	format_configure rpf-wpf 0 0 YUV444M 1024x768 $format

	vsp_runner rpf.0 &
	vsp_runner wpf.0

	local result=$(compare_frames)

	test_complete $result
}

test_main() {
	local format

	for format in $formats ; do
		test_wpf_packing $format
	done
}

test_init $0 "$features"
test_run
