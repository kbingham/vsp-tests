# SPDX-License-Identifier: CC0-1.0

SUBDIRS=data scripts src tests

recursive=all clean install

all:

$(recursive):
	@target=$@ ; \
	for subdir in $(SUBDIRS); do \
		echo "Making $$target in $$subdir" ; \
		$(MAKE) -C $$subdir $$target; \
	done
