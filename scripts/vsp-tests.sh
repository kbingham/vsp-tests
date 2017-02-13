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
	local IFS="$(printf '\n\t')"

	echo "- $script"

	local output=$(./$script 2>&1 | tee /proc/self/fd/2)
	for line in $output ; do
		(echo "$line" | grep -q 'fail$') && num_fail=$((num_fail+1))
		(echo "$line" | grep -q 'pass$') && num_pass=$((num_pass+1))
		(echo "$line" | grep -q 'skipped$') && num_skip=$((num_skip+1))
		num_test=$((num_test+1))
	done

	if [ $(ls *.bin 2>/dev/null | wc -l) != 0 ] ; then
		local dir=$KERNEL_VERSION/test-$script/$iteration/

		mkdir -p $dir
		mv *.bin $dir
	fi
}

run_suite() {
	echo "--- Test loop $1 ---"

	num_fail=0
	num_pass=0
	num_skip=0
	num_test=0

	for test in vsp-unit-test*.sh; do
		run_test $test $1
	done;

	echo "$num_test tests: $num_pass passed, $num_fail failed, $num_skip skipped"
}

for loop in `seq 1 1 $1`; do
	run_suite $loop
done;
