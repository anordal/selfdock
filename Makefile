PREFIX ?= /usr/local
ROOTOVERLAY ?= $(PREFIX)/share/selfdock
CFLAGS = -std=gnu99 -Wall -Wextra -Wpedantic -Os -DROOTOVERLAY=$(ROOTOVERLAY)

selfdock : selfdock.c
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

$(PREFIX)/bin/selfdock : selfdock
	install -o root -g root -m 4111 $^ $@

DEV = $(ROOTOVERLAY)/dev
DEVFILES =\
	$(DEV)/null\

.PHONY: install
install: $(PREFIX)/bin/selfdock | $(DEVFILES)

$(DEV):
	install -d -m555 $@
$(DEV)/% : /dev/% | $(DEV)
	cp -a $^ $@

MODULETESTS=$(wildcard moduletest/*)
.PHONY: test $(MODULETESTS)
test: $(MODULETESTS)
$(MODULETESTS):
	$@ $(subst /,_,$@)
