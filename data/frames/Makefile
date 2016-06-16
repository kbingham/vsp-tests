all:
	./gen-lut.py

clean:
	@rm -f *.bin

install:
	mkdir -p $(INSTALL_DIR)/frames/
	for frame in *.pnm.gz ; do
		gzip -dk $(frame)
		mv ${frame/.gz/} $(INSTALL_DIR)/frames/
	done
	cp *.bin $(INSTALL_DIR)/frames/
