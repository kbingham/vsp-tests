#!/bin/sh

#
# Test all combinations of horizontal flip, vertical flip and rotation on WPF.0.
#

. ./vsp-lib.sh

features="rpf.0 wpf.0 wpf.0[control:'Vertical+Flip']"
optional_features="wpf.0[control:'Horizontal+Flip'] wpf.0[control:'Rotate']"

directions="horizontal vertical rotate"
dir_horizontal_control="Horizontal+Flip"
dir_horizontal_label="hflip"
dir_horizontal_values="0 1"
dir_vertical_control="Vertical+Flip"
dir_vertical_label="vflip"
dir_vertical_values="0 1"
dir_rotate_control="Rotate"
dir_rotate_label="rotate"
dir_rotate_values="0 90"

format="RGB24"

get_var() {
	echo $(eval echo \$dir_$1_$2)
}

set_var() {
	eval dir_$1_$2=$3
}

get_array_value() {
	local index=$2

	echo $1 | cut -d ' ' -f $((index+1))
}

get_array_length() {
	echo $#
}

dir_next_value() {
	# Get the direction name corresponding to the index
	local direction=$(get_array_value "$supported_directions" $1)

	# Get the current value index and increase it
	local value=$(get_var $direction index)
	value=$((value+1))

	# If the index exceeds the possible values array length, reset it to 0.
	if [ $value -ge $(get_array_length $(get_var $direction values)) ] ; then
		value=0
	fi

	# Update the current value index for the direction
	set_var $direction index $value

	# Return whether we have exceeded the maximum
	[ $value != 0 ]
}

dir_set_flipping_control() {
	local direction=$1

	local index=$(get_var $direction index)
	local control=$(get_var $direction control)
	local values=$(get_var $direction values)
	local value=$(get_array_value "$values" $index)

	vsp1_set_control wpf.0 "$control" $value
}

test_flipping() {
	local label=$1

	test_start "$label"

	pipe_configure rpf-wpf 0 0
	format_configure rpf-wpf 0 0 $format 640x480 $format

	vsp_runner rpf.0 &
	vsp_runner wpf.0

	local result=$(compare_frames $label)

	test_complete $result
}

test_main() {
	local direction

	for direction in $directions ; do
		$(vsp1_has_feature "wpf.0[control:'$(get_var $direction control)']") && {
			set_var $direction index 0
			supported_directions="$supported_directions $direction"
		}
	done

	local dir_max=$(get_array_length $supported_directions)
	local dir_current=0

	while true ; do
		# Update all controls
		local label=
		for direction in $supported_directions ; do
			local index=$(get_var $direction index)
			local values=$(get_var $direction values)
			local value=$(get_array_value "$values" $index)
			label="$label $(get_var $direction label)=$value"
			dir_set_flipping_control $direction
		done

		test_flipping "$label"

		while [ $dir_current -lt $dir_max ] ; do
			dir_next_value $dir_current && break
			dir_current=$((dir_current+1))
		done

		if [ $dir_current -ge $dir_max ] ; then
			break
		fi

		dir_current=0
	done
}

test_init $0 "$features" "$optional_features"
test_run
