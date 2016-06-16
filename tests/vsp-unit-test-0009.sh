#!/bin/sh

#
# Test RPF -> WPF with all WPF instances in sequence. The format doesn't matter
# much, use RGB24 to simplify frame comparison.
#

source vsp-lib.sh

features="rpf.0 wpf.0 wpf.1"
optional_features="wpf.1 wpf.2 wpf.3"
format=RGB24

test_wpf() {
	wpf=$1

	test_start "WPF.$wpf"

	pipe_configure rpf-wpf 0 $wpf
	format_configure rpf-wpf 0 $wpf $format 1024x768 $format

	$vsp_runner $mdev input 0 $format &
	$vsp_runner $mdev output $wpf $format

	result=$(compare_frames)

	test_complete $result
}

test_main() {
	num_wpfs=$(vsp1_count_wpfs)

	for wpf in `seq 0 1 $((num_wpfs-1))` ; do
		test_wpf $wpf
	done
}

test_init $0 "$features" "$optional_features"
test_run
