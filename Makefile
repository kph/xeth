INSTALL=/usr/bin/install

default:
	echo "Nothing to do"

distclean:
	rm -f debian/debhelper-build-stamp debian/files debian/*.substvars debian/*.debhelper debian/*.log
	rm -rf debian/.debhelper debian/xeth

bindeb-pkg:
	debuild -uc -us --lintian-opts --profile debian

.PHONY: xeth-kmod bindeb-pkg

