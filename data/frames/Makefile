all:

clean:

install:
	mkdir -p $(INSTALL_DIR)/frames/
	for frame in *.pnm.gz ; do
		gzip -dk $(frame)
		mv ${frame/.gz/} $(INSTALL_DIR)/frames/
	done
