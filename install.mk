#!/usr/bin/make -f
MESON_INSTALL_PREFIX ?= /usr/local
PREFIX ?= $(DESTDIR)$(MESON_INSTALL_PREFIX)
ROOTOVERLAY ?= $(PREFIX)/share/selfdock

DEV = $(ROOTOVERLAY)/dev
DEVFILES =\
	$(DEV)/null\
	$(DEV)/random\
	$(DEV)/urandom\
	$(DEV)/ptmx\

DIRS =\
	$(DEV)/empty\
	$(DEV)/pts\
	$(PREFIX)/bin\
	$(ROOTOVERLAY)/proc\
	$(ROOTOVERLAY)/run\
	$(ROOTOVERLAY)/tmp\

.PHONY: install uninstall
install: $(PREFIX)/bin/selfdock | $(DIRS) $(DEVFILES)
uninstall:
	rm -f $(DEVFILES) $(PREFIX)/bin/selfdock
	rmdir -p --ignore-fail-on-non-empty $(DIRS)

$(DIRS):
	install -d -m555 $@
$(DEVFILES): $(DEV)/% : /dev/% | $(DEV)/empty
	cp -a $^ $@
$(PREFIX)/bin/selfdock : selfdock | $(PREFIX)/bin
	install -o root -g root -m 4111 $^ $@

MODULETESTS=$(wildcard moduletest/*)
.PHONY: test $(MODULETESTS)
test: $(MODULETESTS)
$(MODULETESTS):
	$@ $(PREFIX)/bin/selfdock
