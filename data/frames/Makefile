frames=$(patsubst %.pnm.gz,%.pnm,$(wildcard *.pnm.gz))

all:
	./gen-lut.py

clean:
	@rm -f *.bin

%.pnm: %.pnm.gz
	gzip -dk $<

install: $(frames)
	mkdir -p $(INSTALL_DIR)/frames/
	mv $(frames) $(INSTALL_DIR)/frames/
	cp *.bin $(INSTALL_DIR)/frames/
