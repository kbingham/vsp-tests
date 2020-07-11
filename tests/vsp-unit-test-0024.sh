#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2017 Renesas Electronics Corporation

#
# Test composition through the BRS in RGB and YUV formats.
#

. ./vsp-lib.sh

features="rpf.0 rpf.1 brs wpf.0"
formats="RGB24 YUV444M"

test_brs() {
	local format=$1
	local ninputs=$2

	test_start "BRS in $format with $ninputs inputs"

	pipe_configure rpf-brs $ninputs
	format_configure rpf-brs $format 1024x768 $ninputs

	local input
	for input in `seq 0 1 $((ninputs-1))` ; do
		vsp_runner rpf.$input &
	done
	vsp_runner wpf.0

	local result=$(compare_frames)

	test_complete $result
}

test_main() {
	local num_inputs=2
	local format
	local ninputs

	for format in $formats ; do
		for ninputs in `seq $num_inputs` ; do
			test_brs $format $ninputs
		done
	done
}

test_init $0 "$features"
test_run
