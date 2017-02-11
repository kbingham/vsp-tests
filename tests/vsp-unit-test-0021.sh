#!/bin/sh

#
# Test active pipeline, with high load on CPU/Memory/IO using 'stress'
#
# Test WPF packing in RGB mode. Use a RPF -> WPF pipeline with a fixed ARGB32
# format on the input and capture output frames in all RGB formats supported
# by the WPF.
#

source vsp-lib.sh

features="rpf.0 wpf.0"
formats="RGB332 ARGB555 XRGB555 RGB565 BGR24 RGB24 ABGR32 ARGB32 XBGR32 XRGB32"

test_wpf_packing() {
	test_start "WPF packing in $format during stress testing"

	pipe_configure rpf-wpf 0 0
	format_configure rpf-wpf 0 0 ARGB32 1024x768 $format

	vsp_runner rpf.0 &
	vsp_runner wpf.0

	local result=$(compare_frames)

	test_complete $result
}

exists() {
	type -t "$1" > /dev/null 2>&1
}

test_main() {
	local format

	exists stress || {
		echo "$0: Stress test requires utility 'stress'"
		return
	}

	# Start stressing the system, as a background task
	stress --cpu 8 --io 4 --vm 2 --vm-bytes 128M &

	for format in $formats ; do
		test_wpf_packing $format
	done

	# Recover the system. Stress launches multiple PIDs, so it's best to:
	killall -9 stress
}

test_init $0 "$features"
test_run
