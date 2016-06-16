#!/bin/sh

#
# Test RPF -> WPF with all RPF and WPF instances in sequence. The format
# doesn't matter much, use RGB24 to simplify frame comparison.
#

source vsp-lib.sh

features="rpf.0 rpf.1 wpf.0 wpf.1"
format=RGB24

test_rpf() {
	rpf=$1

	test_start "RPF.$rpf"

	pipe_configure rpf-wpf $rpf 0
	format_configure rpf-wpf $rpf 0 $format 1024x768 $format

	$vsp_runner $mdev input $rpf $format &
	$vsp_runner $mdev output 0 $format

	result=$(compare_frames exact)

	test_complete $result
}

test_wpf() {
	wpf=$1

	test_start "WPF.$wpf"

	pipe_configure rpf-wpf 0 $wpf
	format_configure rpf-wpf 0 $wpf $format 1024x768 $format

	$vsp_runner $mdev input 0 $format &
	$vsp_runner $mdev output $wpf $format

	result=$(compare_frames exact reference $format $wpf)

	test_complete $result
}

test_main() {
	num_rpfs=$(vsp1_count_rpfs)
	num_wpfs=$(vsp1_count_wpfs)

	for rpf in `seq 0 1 $((num_rpfs-1))` ; do
		test_rpf $rpf
	done

	# Skip WPF.0, it has already been tested during the RPF tests.
	for wpf in `seq $((num_wpfs-1))` ; do
		test_wpf $wpf
	done
}

test_init $0 "$features"
test_run
