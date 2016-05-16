#!/bin/sh

#
# Test invalid pipelines, without an RPF or without a WPF.
#

source vsp-lib.sh

features="rpf.0 wpf.0"
format=RGB24

test_no_rpf() {
	test_start "invalid pipeline with no RPF"

	pipe_configure none | ./logger.sh config >> $logfile
	format_configure wpf \
		$format 1024x768 0 | ./logger.sh config >> $logfile

	$vsp_runner $mdev output 0 $format | ./logger.sh input.0 >> $logfile

	# The test always passes if the kernel doesn't crash
	test_complete pass
}

test_no_wpf() {
	test_start "invalid pipeline with no WPF"

	pipe_configure none | ./logger.sh config >> $logfile
	format_configure rpf \
		$format 1024x768 0 | ./logger.sh config >> $logfile

	$vsp_runner $mdev input 0 $format | ./logger.sh input.0 >> $logfile

	# The test always passes if the kernel doesn't crash
	test_complete pass
}

test_run() {
	test_no_rpf
	test_no_wpf
}

test_init $0 "$features"
test_run
