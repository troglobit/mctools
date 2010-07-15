# -*-Makefile-*-
#
# Makefile for mcgen, mdump, and other multicast tools.
#

# VERSION       ?= $(shell git tag -l | tail -1)
VERSION      ?= 1.0.0-rc1
NAME          = mcast-tools
EXECS         = mcgen mdump 
PKG           = $(NAME)-$(VERSION)
ARCHIVE       = $(PKG).tar.bz2

ROOTDIR      ?= $(dir $(shell pwd))
CC           ?= $(CROSS)gcc

prefix       ?= /usr/local
sysconfdir   ?= /etc
datadir       = $(prefix)/share/doc/mcast-tools
mandir        = $(prefix)/share/man/man1

include rules.mk

## Common
CFLAGS        = $(USERCOMPILE)
CFLAGS       += -O2 -W -Wall -Werror
#CFLAGS       += -O -g
LDLIBS        = 
SRCS          = $(addsuffix .c,$(EXECS))
MANS          = $(addsuffix .8,$(EXECS))
DISTFILES     = README AUTHORS LICENSE

all: $(EXECS) ${MSTAT}

install: $(EXECS)
	$(Q)[ -n "$(DESTDIR)" -a ! -d $(DESTDIR) ] || install -d $(DESTDIR)
	$(Q)install -d $(DESTDIR)$(prefix)/sbin
	$(Q)install -d $(DESTDIR)$(sysconfdir)
	$(Q)install -d $(DESTDIR)$(datadir)
	$(Q)install -d $(DESTDIR)$(mandir)
	$(Q)for file in $(EXECS); do \
		install -m 0755 $$file $(DESTDIR)$(prefix)/sbin/$$file; \
	done
	$(Q)install --backup=existing -m 0644 $(CONFIG) $(DESTDIR)$(sysconfdir)/$(CONFIG)
	$(Q)for file in $(DISTFILES); do \
		install -m 0644 $$file $(DESTDIR)$(datadir)/$$file; \
	done
	$(Q)for file in $(MANS); do \
		install -m 0644 $$file $(DESTDIR)$(mandir)/$$file; \
	done

uninstall:
	-$(Q)for file in $(EXECS); do \
		$(RM) $(DESTDIR)$(prefix)/sbin/$$file; \
	done
	-$(Q)$(RM) $(DESTDIR)$(sysconfdir)/$(CONFIG)
	-$(Q)$(RM) -r $(DESTDIR)$(datadir)
	-$(Q)for file in $(MANS); do \
		$(RM) $(DESTDIR)$(mandir)/$$file; \
	done

clean: ${SNMPCLEAN}
	-$(Q)$(RM) $(OBJS) $(EXECS)

distclean:
	-$(Q)$(RM) $(OBJS) core $(EXECS) vers.c cfparse.c tags TAGS *.o *.map .*.d *.out tags TAGS

dist:
	@echo "Building bzip2 tarball of $(PKG) in parent dir..."
	git archive --format=tar --prefix=$(PKG)/ $(VERSION) | bzip2 >../$(ARCHIVE)
	@(cd ..; md5sum $(ARCHIVE))

TAGS:
	@etags ${SRCS}
