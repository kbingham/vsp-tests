#!/bin/sh

#
# Test RGB to HSV conversion: Use a RPF -> HST -> WPF pipeline with a fixed
# ARGB32 format on the input and capture output frames in all HSV formats
# supported by the WPF.
#
# Test HSV to HSV pass-through: Use a RPF -> WPF pipeline with identical HSV
# formats on the input and output.
#

source vsp-lib.sh

features="rpf.0 hst wpf.0"
formats="HSV24 HSV32"

test_rgb_to_hsv() {
	local format=$1

	test_start "RGB to $format conversion"

	pipe_configure rpf-hst
	format_configure rpf-hst ARGB32 1024x768 $format

	vsp_runner rpf.0 &
	vsp_runner wpf.0

	result=$(compare_frames)

	test_complete $result
}

test_hsv_to_hsv() {
	local format=$1

	test_start "HSV pass-through in $format"

	pipe_configure rpf-wpf 0 0
	format_configure rpf-wpf 0 0 $format 1024x768 $format

	vsp_runner rpf.0 &
	vsp_runner wpf.0

	result=$(compare_frames)

	test_complete $result
}

test_main() {
	if [ $(vsp1_generation) != VSP1 ] ; then
		exit 1
	fi

	for format in $formats ; do
		test_rgb_to_hsv $format
		test_hsv_to_hsv $format
	done
}

test_init $0 "$features"
test_run
