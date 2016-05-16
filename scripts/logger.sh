#!/bin/sh

now() {
	awk '/^now/ {time=$3; printf("[%u.%06u]", time / 1000000000, (time % 1000000000) / 1000) ; exit}' /proc/timer_list
}

label=${1:+ [$1]}

while read line ; do
	echo "$(now)$label $line"
done
