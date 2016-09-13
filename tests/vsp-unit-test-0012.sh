#!/bin/sh

#
# Test runtime modification of horizontal and vertical flipping on WPF.0.
#

source vsp-lib.sh

features="rpf.0 wpf.0 wpf.0[control:'Vertical+Flip']"
optional_features="wpf.0[control:'Horizontal+Flip']"

directions="horizontal vertical"
dir_horizontal_control="Horizontal+Flip"
dir_horizontal_label="hflip"
dir_vertical_control="Vertical+Flip"
dir_vertical_label="vflip"

format="RGB24"

get_var() {
	echo $(eval echo \$dir_$1_$2)
}

flip_control() {
	local direction=$1
	local value=$2
	local control=$(get_var $direction control)

	vsp1_set_control wpf.0 "$control" $value
}

reset_controls() {
	# Reset all controls to their default value
	local direction

	for direction in $* ; do
		flip_control $direction 0
	done
}

test_flipping() {
	local direction=$1
	local label="$(get_var $direction label)"

	test_start "$label"

	pipe_configure rpf-wpf 0 0
	format_configure rpf-wpf 0 0 $format 1024x768 $format

	vsp_runner rpf.0 --count=6 &
	vsp_runner wpf.0 --count=6 --skip=0 --buffers=1 --pause=3 &

	vsp_runner_wait wpf.0
	local result=$(compare_frames $label=0)

	[ $result = fail ] && {
		test_complete $result ;
		return
	}

	flip_control $direction 1

	vsp_runner_resume wpf.0
	wait

	result=$(compare_frames $label=1)

	test_complete $result
}

test_main() {
	# Check the supported directions and reset the associated controls
	local supported_directions
	local direction

	for direction in $directions ; do
		$(vsp1_has_feature "wpf.0[control:'$(get_var $direction control)']") && {
			supported_directions="$supported_directions $direction" ;
			flip_control $direction 0
		}
	done

	# Reset the rotation control to avoid interfering with the test
	$(vsp1_has_feature "wpf.0[control:'Rotate']") && {
		vsp1_set_control wpf.0 Rotate 0
	}

	local directions=$supported_directions

	for direction in $directions ; do
		reset_controls $directions
		test_flipping $direction
	done
}

test_init $0 "$features" "$optional_features"
test_run
