#!/bin/sh

##
## VSP Tests runner
##
## Automatically execute all vsp-unit tests
## Move test failure results to a specific folder for
## the running kernel version
##
## An argument can be provided to specify the number of
## iterations to perform
##
## usage:
##  ./vsp-tests.sh <n>
##
##   n: Number of iterations to execute test suite
##

KERNEL_VERSION=`uname -r`

run_test() {
	local script=$1
	local iteration=$2

	echo "- $script"
	./$script

	if [ $(ls *.bin 2>/dev/null | wc -l) != 0 ] ; then
		local dir=$KERNEL_VERSION/test-$script/$iteration/

		mkdir -p $dir
		mv *.bin $dir
	fi
}

run_suite() {
	echo "--- Test loop $1 ---"

	for test in vsp-unit-test*.sh; do
		run_test $test $1
	done;
}

for loop in `seq 1 1 $1`; do
	run_suite $loop
done;
