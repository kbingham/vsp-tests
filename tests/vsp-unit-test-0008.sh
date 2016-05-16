#!/bin/sh

#
# Test downscaling and upscaling in RGB and YUV modes with a BRU inserted in
# the pipeline, both before and after the scaler.
#

source vsp-lib.sh

features="bru rpf.0 uds wpf.0"
formats="RGB24 UYVY"

test_scale() {
	format=$1
	insize=$2
	outsize=$3
	order=$4

	if [ $order = 'after' ] ; then
		pipe=rpf-bru-uds
	else
		pipe=rpf-uds-bru
	fi

	test_start "scaling from $insize to $outsize in $format $order BRU"

	pipe_configure $pipe | ./logger.sh config >> $logfile
	format_configure $pipe \
		$format $insize $format $outsize | ./logger.sh config >> $logfile

	$vsp_runner $mdev input 0 $format  | ./logger.sh input.0  >> $logfile &
	$vsp_runner $mdev output 0 $format | ./logger.sh output.0 >> $logfile

	result=$(compare_frames exact scaled $format 0)

	test_complete $result
}

test_run() {
	for format in $formats ; do
		test_scale $format 1024x768 640x480 before
		test_scale $format 640x480 1024x768 before
		test_scale $format 1024x768 640x480 after
		test_scale $format 640x480 1024x768 after
	done
}

test_init $0 "$features"
test_run
