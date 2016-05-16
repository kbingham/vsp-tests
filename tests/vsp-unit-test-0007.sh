#!/bin/sh

#
# Test composition through the BRU in RGB and YUV formats.
#

source vsp-lib.sh

features="rpf.0 rpf.1 bru wpf.0"
formats="RGB24 UYVY"

test_bru() {
	format=$1
	ninputs=$2

	test_start "BRU in $format with $ninputs inputs"

	pipe_configure rpf-bru $ninputs | ./logger.sh config >> $logfile
	format_configure rpf-bru \
		$format 1024x768 $ninputs | ./logger.sh config >> $logfile

	for input in `seq 0 1 $((ninputs-1))` ; do
		$vsp_runner $mdev input $input $format | ./logger.sh input.$input >> $logfile &
	done
	$vsp_runner $mdev output 0 $format | ./logger.sh output.0 >> $logfile

	result=$(compare_frames fuzzy composed-$ninputs $format 0)

	test_complete $result
}

test_run() {
	max_inputs=$(vsp1_count_bru_inputs)

	for format in $formats ; do
		for ninputs in `seq $max_inputs` ; do
			test_bru $format $ninputs
		done
	done
}

test_init $0 "$features"
test_run
