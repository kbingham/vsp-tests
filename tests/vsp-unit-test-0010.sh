#!/bin/sh

#
# Test CLU and LUT in RGB and YUV modes. Use a RPF -> CLU -> WPF and
# RPF -> LUT -> WPF pipelines with identical input and output formats.
#

. ./vsp-lib.sh

features="rpf.0 clu lut wpf.0"
formats="RGB24 YUV444M"

lut_types="clu lut"

# Keep the "zero" configuration first to catch lack of hardware table setup
# due to V4L2 control caching, as the initial value of the LUT and CLU table
# controls is all 0.
clu_configs="zero identity wave"
lut_configs="zero identity gamma"

test_lut() {
	local lut_type=$1
	local format=$2
	local config=$3

	test_start "$(echo $lut_type | tr [:lower:] [:upper:]) in $format with $config configuration"

	local config_file=frames/${lut_type}-${config}.bin

	pipe_configure rpf-${lut_type}
	format_configure rpf-${lut_type} $format 1024x768

	vsp1_set_control $lut_type "Look-Up+Table" "<$config_file"

	vsp_runner rpf.0 &
	vsp_runner wpf.0

	local result=$(compare_frames $lut_type=$config_file)

	test_complete $result
}

test_main() {
	local lut
	local format
	local config

	for lut in $lut_types ; do
		local configs=$(eval echo \$${lut}_configs)
		for format in $formats ; do
			for config in $configs ; do
				test_lut $lut $format $config
			done
		done
	done
}

test_init $0 "$features"
test_run
