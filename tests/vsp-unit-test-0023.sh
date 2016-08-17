#!/bin/sh

#
# Test 2D histogram generation. Use a RPF -> HST -> HSI -> WPF pipeline
# with the HGT hooked up at the HST output.
#

source vsp-lib.sh

features="hgt hsi hst rpf.0 wpf.0"

test_histogram() {
	local hue_areas=$1

	test_start "histogram HGT with hue areas $hue_areas"

	pipe_configure rpf-hgt
	format_configure rpf-hgt RGB24 1024x768
	hgt_configure "$hue_areas"

	vsp_runner hgt &
	vsp_runner rpf.0 &
	vsp_runner wpf.0

	result=$(compare_histograms $hue_areas)

	test_complete $result
}

test_main() {
	test_histogram "0,255,255,255,255,255,255,255,255,255,255,255"
	test_histogram "0,40,40,80,80,120,120,160,160,200,200,255"
	test_histogram "220,40,40,80,80,120,120,160,160,200,200,220"
	test_histogram "0,10,50,60,100,110,150,160,200,210,250,255"
	test_histogram "10,20,50,60,100,110,150,160,200,210,230,240"
	test_histogram "240,20,60,80,100,120,140,160,180,200,210,220"
}

test_init $0 "$features"
test_run
