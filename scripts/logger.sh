#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2016 Renesas Electronics Corporation

label=${1:+ [$1]}

TRACE_MARKER=/sys/kernel/debug/tracing/trace_marker
if [ -e $TRACE_MARKER ] && [ "$(id -u)" = 0 ]; then
	./monotonic-ts "$label" | tee -a $TRACE_MARKER
else
	./monotonic-ts "$label"
fi
