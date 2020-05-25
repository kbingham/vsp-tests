#!/bin/sh

#
# Test unbinding and binding all VSP1 devices, performing a simple
# copy test to validate the hardware afterwards.
#

. ./vsp-lib.sh

features="rpf.0 wpf.0"

vsp1_driver=/sys/bus/platform/drivers/vsp1
vsps=$(cd /sys/bus/platform/devices/; ls | grep vsp)

unbind_vsp() {
	echo $1 > $vsp1_driver/unbind
}

bind_vsp() {
	echo $1 > $vsp1_driver/bind
}

# Input is directly copied to the output. No change in format or size.
test_copy() {
	local format=$1
	local insize=$2

	test_start "simple hardware validation after unbind/bind cycles"

	pipe_configure rpf-wpf 0 0
	format_configure rpf-wpf 0 0 $format $insize $format

	vsp_runner rpf.0 &
	vsp_runner wpf.0

	local result=$(compare_frames)

	test_complete $result
}

test_main() {
	local format

	# Unbind and rebind VSPs individually
	for v in $vsps; do
		unbind_vsp $v
		bind_vsp $v
	done

	# Perform a simple copy test to validate HW is alive
	test_copy RGB24 128x128
}

test_init $0 "$features"
test_run
