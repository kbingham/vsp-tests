#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2016-2017 Renesas Electronics Corporation

#
# Test RPF -> WPF with all WPF instances in sequence. The format doesn't matter
# much, use RGB24 to simplify frame comparison.
#

. ./vsp-lib.sh

features="rpf.0 wpf.0 wpf.1"
optional_features="wpf.1 wpf.2 wpf.3"
format=RGB24

test_wpf() {
	local wpf=$1

	test_start "WPF.$wpf"

	pipe_configure rpf-wpf 0 $wpf
	format_configure rpf-wpf 0 $wpf $format 1024x768 $format

	vsp_runner rpf.0 &
	vsp_runner wpf.$wpf

	local result=$(compare_frames)

	test_complete $result
}

test_main() {
	local num_wpfs=$(vsp1_count_wpfs)
	local wpf

	for wpf in `seq 0 1 $((num_wpfs-1))` ; do
		test_wpf $wpf
	done
}

test_init $0 "$features" "$optional_features"
test_run
