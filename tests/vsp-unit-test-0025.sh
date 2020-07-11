#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2018 Renesas Electronics Corporation

#
# Test pipelines which have a single pixel dimension. Use a RPF -> WPF
# pipeline with identical input and output formats to generate our output.
#

. ./vsp-lib.sh

features="rpf.0 uds wpf.0"
formats="RGB24 ARGB32"

# Input is directly copied to the output. No change in format or size.
test_copy() {
	local format=$1
	local insize=$2

	test_start "copying $insize in $format"

	pipe_configure rpf-wpf 0 0
	format_configure rpf-wpf 0 0 $format $insize $format

	vsp_runner rpf.0 &
	vsp_runner wpf.0

	local result=$(compare_frames)

	test_complete $result
}

test_main() {
	local format

	for format in $formats ; do
		test_copy $format 128x1

		# Skipped : Test framework does not yet support strides != width
		#test_copy $format 1x128
	done
}

test_init $0 "$features"
test_run
