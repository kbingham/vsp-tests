#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2016 Renesas Electronics Corporation

now() {
	awk '/^now/ {time=$3; printf("[%u.%06u]", time / 1000000000, (time % 1000000000) / 1000) ; exit}' /proc/timer_list
}

label=${1:+ [$1]}

TRACE_MARKER=/sys/kernel/debug/tracing/trace_marker
if [ -e $TRACE_MARKER ]; then
	extra_log_files=$TRACE_MARKER
fi

while read line ; do
	newline="$(now)$label $line"

	echo "$newline"

	for f in $extra_log_files; do
		echo "$newline" >> $f;
	done;
done
