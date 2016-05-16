#!/bin/sh

#
# Test downscaling and upscaling in RGB and YUV modes. Use a RPF -> UDS -> WPF
# pipeline with identical input and output formats.
#

source vsp-lib.sh

features="rpf.0 uds wpf.0"
formats="RGB24 UYVY"

test_scale() {
	format=$1
	insize=$2
	outsize=$3

	test_start "scaling from $insize to $outsize in $format"

	pipe_configure rpf-uds | ./logger.sh config >> $logfile
	format_configure rpf-uds \
		$format $insize $format $outsize | ./logger.sh config >> $logfile

	$vsp_runner $mdev input 0 $format  | ./logger.sh input.0  >> $logfile &
	$vsp_runner $mdev output 0 $format | ./logger.sh output.0 >> $logfile

	result=$(compare_frames exact scaled $format 0)

	test_complete $result
}

test_run() {
	for format in $formats ; do
		test_scale $format 1024x768 640x480
		test_scale $format 640x480 1024x768
	done
}

test_init $0 "$features"
test_run
