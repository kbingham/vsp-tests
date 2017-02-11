#!/bin/sh

#
# Test active pipeline, with high load on CPU/Memory/IO using 'stress'
#
# Utilise the same test as for suspend resume testing, to verify a longer
# duration pipeline lifetime while we stress the system.
#

source vsp-lib.sh

features="rpf.0 wpf.0"

# This extended function performs the same
# as it's non-extended name-sake - but runs the pipeline
# for 300 frames.

test_extended_wpf_packing() {
	pipe_configure rpf-wpf 0 0
	format_configure rpf-wpf 0 0 ARGB32 1024x768 RGB24

	vsp_runner rpf.0 --count=300 &
	vsp_runner wpf.0 --count=300 --skip=297

	local result=$(compare_frames)

	if [ x$result == x"pass" ] ; then
		return 0;
	else
		return 1;
	fi
}

exists() {
	type -t "$1" > /dev/null 2>&1;
}

test_main() {
	test_start "long duration pipelines under stress"

	exists stress || {
		echo "$0: Stress test requires utility 'stress'"
		test_complete skip
		return
	}

	# Start stressing the system, as a background task
	stress --cpu 8 --io 4 --vm 2 --vm-bytes 128M &

	if test_extended_wpf_packing ; then
		test_complete pass
	else
		test_complete fail
	fi

	# Recover the system. Stress launches multiple PIDs, so it's best to:
	killall -9 stress
}

test_init $0 "$features"
test_run
