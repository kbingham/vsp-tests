#!/bin/sh

# Report testing conditions

model=`cat /sys/firmware/devicetree/base/model`

echo "Test Conditions:"

check_all() {
	echo "  Date:		" `date`
	echo "  Platform:	" "$model"
	echo "  Kernel release:	" `uname -r`
	echo "  convert: 	" `which convert`
	echo "  compare:	" `which compare`
	echo "  killall:	" `which killall`
	echo "  raw2rgbpnm:	" `which raw2rgbpnm`
	echo "  stress:		" `which stress`
	echo "  yavta:		" `which yavta`
}

check_all | column -ts "	"
