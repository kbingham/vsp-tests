#!/bin/sh

#
# Test WPF packing in RGB mode. Use a RPF -> WPF pipeline with a fixed ARGB32
# format on the input and capture output frames in all RGB formats supported
# by the WPF.
#

source vsp-lib.sh

features="rpf.0 wpf.0"
formats="RGB332 ARGB555 XRGB555 RGB565 BGR24 RGB24 ABGR32 ARGB32 XBGR32 XRGB32"

test_wpf_packing() {
	test_start "WPF packing in $format"

	pipe_configure rpf-wpf 0 0
	format_configure rpf-wpf 0 0 ARGB32 1024x768 $format

	$vsp_runner $mdev input 0 ARGB32 &
	$vsp_runner $mdev output 0 $format

	result=$(compare_frames)

	test_complete $result
}

test_main() {
	for format in $formats ; do
		test_wpf_packing $format
	done
}

test_init $0 "$features"
test_run
