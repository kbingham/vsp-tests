# SPDX-License-Identifier: CC0-1.0

SUBDIRS=frames

recursive=all clean install

all:

$(recursive):
	@target=$@ ; \
	for subdir in $(SUBDIRS); do \
		echo "Making $$target in $$subdir" ; \
		$(MAKE) -C $$subdir $$target; \
	done
