#!/bin/sh

#
# Test RPF -> WPF with all RPF instances in sequence. The format doesn't matter
# much, use RGB24 to simplify frame comparison.
#

source vsp-lib.sh

features="rpf.0 rpf.1 wpf.0"
optional_features="rpf.2 rpf.3 rpf.4"
format=RGB24

test_rpf() {
	rpf=$1

	test_start "RPF.$rpf"

	pipe_configure rpf-wpf $rpf 0
	format_configure rpf-wpf $rpf 0 $format 1024x768 $format

	vsp_runner rpf.$rpf &
	vsp_runner wpf.0

	result=$(compare_frames)

	test_complete $result
}

test_main() {
	num_rpfs=$(vsp1_count_rpfs)

	for rpf in `seq 0 1 $((num_rpfs-1))` ; do
		test_rpf $rpf
	done
}

test_init $0 "$features" "$optional_features"
test_run
