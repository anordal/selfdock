selfdock : selfdock.c
	$(CC) -std=gnu99 $^ -o $@

.PHONY: install
install: /usr/local/bin/selfdock | /usr/local/share/selfdock/dev/null

/usr/local/bin/selfdock : selfdock
	install -o root -g root -m 4111 $^ /usr/local/bin/

/usr/local/share/selfdock/dev/null:
	mkdir -p /usr/local/share/selfdock/dev
	mknod $@_tmp c 1 3
	chmod 666 $@_tmp
	mv $@_tmp $@

MODULETESTS=$(wildcard moduletest/*)
.PHONY: test $(MODULETESTS)
test: $(MODULETESTS)
$(MODULETESTS):
	$@ $(subst /,_,$@)
