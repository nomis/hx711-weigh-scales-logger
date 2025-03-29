.PHONY: all clean upload htdocs src_htdocs
.DELETE_ON_ERROR:

all:
	platformio run

clean:
	platformio run -t clean
	rm -rf .pio
	rm -f src/htdocs/*.gz.h

upload:
	platformio run -t upload

htdocs: \
	$(patsubst %.xml,src/%.xml.gz.h,$(wildcard htdocs/*.xml))

src_htdocs:
	mkdir -p src/htdocs

src/htdocs/%.gz.h: htdocs/% gzip-embed.py | src_htdocs
	./gzip-embed.py $< $@
