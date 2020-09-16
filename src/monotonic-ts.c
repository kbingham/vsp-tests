/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2020 Kieran Bingham <kieran.bingham@ideasonboard.com> */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char ** argv)
{
	struct timespec tp;
	char *line = NULL;
	size_t size = 0;
	const char *label = "";

	if (argc > 1)
		label = argv[1];

	/*
	 * Explicitly set line buffering on stdin to be sure it is delivered
	 * in a timely fashion for our timestamping purposes when data is fed
	 * through a pipe.
	 */
	setlinebuf(stdin);

	do {
		if (getline(&line, &size, stdin) <= 0)
			break;

		clock_gettime(CLOCK_MONOTONIC, &tp);
		printf("[%ld.%.9ld]%s %s", tp.tv_sec, tp.tv_nsec, label, line);
	} while (!feof(stdin));

	free(line);

	return 0;
}
