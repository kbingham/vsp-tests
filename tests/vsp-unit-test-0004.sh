#!/bin/sh

#
# Test 1D histogram generation. Use a RPF -> WPF pipeline with the HGO hooked
# up at the RPF output.
#

source vsp-lib.sh

features="hgo rpf.0 wpf.0"
formats="RGB24 UYVY"

test_histogram() {
	test_start "histogram in $format"

	pipe_configure rpf-hgo                    | ./logger.sh config >> $logfile
	format_configure rpf-hgo \
		$format 1024x768                  | ./logger.sh config >> $logfile

	$vsp_runner $mdev m2m-hgo $format $format | ./logger.sh config >> $logfile
	$vsp_runner $mdev hgo                     | ./logger.sh hgo >> $logfile &
	$vsp_runner $mdev input 0 $format         | ./logger.sh input.0 >> $logfile &
	$vsp_runner $mdev output 0 $format        | ./logger.sh output.0 >> $logfile

	result=$(compare_histograms $format 0)

	test_complete $result
}

test_run() {
	for format in $formats ; do
		test_histogram $format
	done
}

test_init $0 "$features"
test_run
