#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2016-2017 Renesas Electronics Corporation

#
# Test power-management suspend/resume whilst pipelines are idle
#
# Utilise the basic RPF->WPF packing test case as a measure that the hardware
# is operable while we perform test suspend and resume, and verify that it is
# still operable after resume.
#
# Format iteration loops are maintained, even with only one format so that this
# test can be easily extended to try further formats if needed in the future.
#

. ./vsp-lib.sh

features="rpf.0 wpf.0"

# Two formats are specified so that the test is run twice.
# This ensures that stop -> start works both before and after suspend
formats="RGB24 RGB24"

# These can be extracted from /sys/power/pm_test
suspend_modes="freezer devices platform processors core"

test_wpf_packing() {
	local format=$1

	pipe_configure rpf-wpf 0 0
	format_configure rpf-wpf 0 0 ARGB32 1024x768 $format

	vsp_runner rpf.0 &
	vsp_runner wpf.0

	local result=$(compare_frames)
	[ x$result == xpass ] && return 0 || return 1
}

test_hw_pipe() {
	local format
	local result

	for format in $formats ; do
		test_wpf_packing $format
		result=$?

		# return early on failure
		[ $result != 0 ] && return 1
	done

	return 0
}

test_suspend_resume() {
	local result=0

	test_start "non-active pipeline suspend/resume in suspend:$mode"

	# Verify the test is available
	grep -q $mode /sys/power/pm_test
	if [ $? != 0 ]; then
		test_complete skip
		return
	fi

	# Test the hardware each side of suspend resume
	test_hw_pipe
	result=$((result+$?))

	# Set the test mode
	echo $mode > /sys/power/pm_test

	# Commence suspend
	# The pm_test framework will automatically resume after 5 seconds
	echo mem > /sys/power/state

	# Verify the hardware is still operational
	test_hw_pipe
	result=$((result+$?))

	if [ $result != 0 ]; then
		test_complete "failed"
	else
		test_complete "passed"
	fi
}

test_main() {
	local mode

	# Check for pm-suspend test option
	if [ ! -e /sys/power/pm_test ] ; then
		echo "$0: suspend/resume testing requires CONFIG_PM_DEBUG"
		return
	fi

	for mode in $suspend_modes ; do
		test_suspend_resume $mode
	done
}

test_init $0 "$features"
test_run
