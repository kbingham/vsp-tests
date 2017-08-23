#!/bin/sh

#
# Test RPF unpacking in RGB mode. Use a RPF -> WPF pipeline with a fixed ARGB32
# format on the output and feed frames to the VSP in all RGB formats supported
# by the RPF.
#

. ./vsp-lib.sh

features="rpf.0 wpf.0"
formats="RGB332 ARGB555 XRGB555 RGB565 BGR24 RGB24 ABGR32 ARGB32 XBGR32 XRGB32"

test_rpf_unpacking() {
	test_start "RPF unpacking in $format"

	pipe_configure rpf-wpf 0 0
	format_configure rpf-wpf 0 0 $format 1024x768 ARGB32

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
