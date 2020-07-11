#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2018 Renesas Electronics Corporation

# Report testing conditions

model=$(cat /sys/firmware/devicetree/base/model)

echo "Test Conditions:"

echo "  Platform:       " "$model"
echo "  Kernel release: " $(uname -r)
echo "  convert:        " $(which convert)
echo "  compare:        " $(which compare)
echo "  killall:        " $(which killall)
echo "  raw2rgbpnm:     " $(which raw2rgbpnm)
echo "  stress:         " $(which stress)
echo "  yavta:          " $(which yavta)
