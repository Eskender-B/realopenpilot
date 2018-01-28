CFLAGS += -std=c11 -Wall -Wextra -pedantic -Werror
PREFIX ?= /data/data/com.termux/files/usr

live-camera: live-camera.c

install: live-camera
	mkdir -p $(PREFIX)/bin/ $(PREFIX)/libexec/
	install live-camera $(PREFIX)/libexec/
	install scripts/* $(PREFIX)/bin/

.PHONY: install
