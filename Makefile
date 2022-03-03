PREFIX=/usr/local/bin

dedup: dedup.c
	$(CC) -g -Wall $< -o $@

install:
	install dedup $(PREFIX)/dedup

uninstall:
	rm -f $(PREFIX)/dedup

dist: clean
	rm -f dedup.tar.gz && cd .. && \
        tar czvf dedup/dedup.tar.gz dedup/*

clean:
	rm -f dedup *.tar.gz *.asc
