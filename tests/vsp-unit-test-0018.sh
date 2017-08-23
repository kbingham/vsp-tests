#!/bin/sh

#
# Test RPF crop using RGB. Use a RPF -> WPF pipeline, passing a selection of
# cropping windows.
#

. ./vsp-lib.sh

features="rpf.0 wpf.0"
crops="(0,0)/512x384 (32,32)/512x384 (32,64)/512x384 (64,32)/512x384"

test_rpf_cropping() {
	test_start "RPF crop from $crop"

	pipe_configure rpf-wpf 0 0
	format_configure rpf-wpf 0 0 RGB24 1024x768 ARGB32 --rpfcrop=$crop

	vsp_runner rpf.0 &
	vsp_runner wpf.0

	local result=$(compare_frames crop=${crop})

	test_complete $result
}

test_main() {
	local crop

	for crop in $crops ; do
		test_rpf_cropping $crop
	done
}

test_init $0 "$features"
test_run
