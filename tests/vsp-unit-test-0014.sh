#!/bin/sh

#
# Test RPF unpacking in RGB mode. Use a RPF -> WPF pipeline with a fixed YUV444M
# format on the output and feed frames to the VSP in all YUV formats supported
# by the RPF.
#

. ./vsp-lib.sh

features="rpf.0 wpf.0"
formats="NV12M NV16M NV21M NV61M UYVY VYUY YUV420M YUV422M YUV444M YVU420M YVU422M YVU444M YUYV YVYU"

test_rpf_unpacking() {
	test_start "RPF unpacking in $format"

	if [ $format = VYUY -a $(vsp1_generation) != VSP1 ] ; then
		test_complete skip
		return
	fi

	pipe_configure rpf-wpf 0 0
	format_configure rpf-wpf 0 0 $format 1024x768 YUV444M

	vsp_runner rpf.0 &
	vsp_runner wpf.0

	local result=$(compare_frames)

	test_complete $result
}

test_main() {
	local format

	for format in $formats ; do
		test_rpf_unpacking $format
	done
}

test_init $0 "$features"
test_run
