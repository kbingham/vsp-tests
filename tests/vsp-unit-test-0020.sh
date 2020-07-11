#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2016-2018 Renesas Electronics Corporation

#
# Test power-management suspend/resume whilst pipelines are active
#
# Utilise the basic RPF->WPF packing test case as a measure that the hardware
# is operable while we perform test suspend and resume, and verify that it is
# still successful even with a suspend resume cycle in the middle of the test.
#

. ./vsp-lib.sh

features="rpf.0 wpf.0"

# These can be extracted from /sys/power/pm_test
suspend_modes="freezer devices platform processors core"

# This extended function performs the same as it's non-extended name-sake, but
# runs the pipeline for a configurable number of frames.
test_extended_wpf_packing() {
	local format=$1
	local num_frames=$2

	pipe_configure rpf-wpf 0 0
	format_configure rpf-wpf 0 0 ARGB32 1024x768 $format

	vsp_runner rpf.0 --count=$num_frames &
	vsp_runner wpf.0 --count=$num_frames --skip=$((num_frames-3))

	local result=$(compare_frames)
	[ x$result == xpass ] && return 0 || return 1
}

test_hw_pipe() {
	# Run the pipeline for 1000 frames. The suspend action should occur in
	# the middle.
	test_extended_wpf_packing RGB24 1000
}

test_suspend_resume() {
	local result
	local test_pid

	test_start "Testing active pipeline suspend/resume in suspend:$mode"

	# Verify the test is available
	grep -q $mode /sys/power/pm_test
	if [ $? != 0 ]; then
		test_complete skip
		return
	fi

	# Set the hardware running in parallel while we suspend
	test_hw_pipe &
	test_pid=$!

	# Make sure the pipeline has time to start
	sleep 1

	# Set the test mode
	echo $mode > /sys/power/pm_test

	# Commence suspend
	# The pm_test framework will automatically resume after 5 seconds
	echo mem > /sys/power/state

	# Wait for the pipeline to complete
	wait $test_pid
	result=$?

	if [ $result == 0 ]; then
		test_complete pass
	else
		test_complete fail
	fi
}

test_main() {
	local mode

	# Check for pm-suspend test option
	if [ ! -e /sys/power/pm_test ] ; then
		echo "$0: suspend/resume testing requires CONFIG_PM_DEBUG"
		return
	fi;

	for mode in $suspend_modes ; do
		test_suspend_resume $mode
	done;
}

test_init $0 "$features"
test_run
