# Halibut master makefile

# Currently depends on gcc, because:
#  - the dependency tracking uses -MD in order to avoid needing an
#    explicit `make depend' step
#  - the definition of CFLAGS includes the gcc-specific flag
#    `-Wall'
#
# Currently depends on GNU make, because:
#  - the Makefile uses GNU ifdef / ifndef commands and GNU make `%'
#    pattern rules
#  - we use .PHONY

prefix=/usr/local
exec_prefix=$(prefix)
bindir=$(exec_prefix)/bin
INSTALL=install -c

.PHONY: all install clean spotless topclean release

ifdef RELEASE
ifndef VERSION
VERSION := $(RELEASE)
endif
else
CFLAGS += -g
endif

ifeq (x$(VERSION)y,xy)
RELDIR := halibut
else
RELDIR := halibut-$(VERSION)
endif

# `make' from top level will build in directory `build'
# `make BUILDDIR=foo' from top level will build in directory foo
ifndef REALBUILD
ifndef BUILDDIR
ifdef TEST
BUILDDIR := test
else
BUILDDIR := build
endif
endif

all install:
	@test -d $(BUILDDIR) || mkdir $(BUILDDIR)
	@$(MAKE) -C $(BUILDDIR) -f ../Makefile $@ REALBUILD=yes

spotless: topclean
	@test -d $(BUILDDIR) || mkdir $(BUILDDIR)
	@$(MAKE) -C $(BUILDDIR) -f ../Makefile spotless REALBUILD=yes

clean: topclean
	@test -d $(BUILDDIR) || mkdir $(BUILDDIR)
	@$(MAKE) -C $(BUILDDIR) -f ../Makefile clean REALBUILD=yes

# Remove Halibut output files in the source directory (may
# have been created by running, for example, `build/halibut
# inputs/test.but').
topclean:
	rm -f *.html output.* *.tar.gz

# Make a release archive. If $(VERSION) is specified, this will
# also contain a `manifest' file which will be used to decide the
# version number automatically.
release: release.sh
	./release.sh $(RELDIR) $(VERSION)

else

# The `real' makefile part.

CFLAGS += -Wall -W -ansi -pedantic

ifdef TEST
CFLAGS += -DLOGALLOC
LIBS += -lefence
endif

ifndef VER
ifdef VERSION
VER := $(VERSION)
endif
endif
ifdef VER
VDEF = -DVERSION=\"$(VER)\"
else
VDEF = `(cd $(SRC); md5sum -c manifest >& /dev/null && cat version)`
endif

all: halibut

SRC := ../

ifeq ($(shell test -d $(SRC)charset && echo yes),yes)
LIBCHARSET_SRCDIR = $(SRC)charset/
else
LIBCHARSET_SRCDIR = $(SRC)../charset/
endif
LIBCHARSET_OBJDIR = ./#
LIBCHARSET_OBJPFX = cs-#
LIBCHARSET_GENPFX = charset-#
MD = -MD
CFLAGS += -I$(LIBCHARSET_SRCDIR) -I$(LIBCHARSET_OBJDIR)
include $(LIBCHARSET_SRCDIR)Makefile

MODULES := main malloc ustring error help licence version misc tree234
MODULES += input in_afm in_pf in_sfnt keywords contents index biblio
MODULES += bk_text bk_html bk_whlp bk_man bk_info bk_paper bk_ps bk_pdf
MODULES += winhelp deflate psdata wcwidth

OBJECTS := $(addsuffix .o,$(MODULES)) $(LIBCHARSET_OBJS)
DEPS := $(addsuffix .d,$(MODULES))

halibut: $(OBJECTS)
	$(CC) $(LFLAGS) -o halibut $(OBJECTS) $(LIBS)

%.o: $(SRC)%.c
	$(CC) $(CFLAGS) -MD -c $<

version.o: FORCE
	$(CC) $(VDEF) -MD -c $(SRC)version.c

spotless:: clean
	rm -f *.d

clean::
	rm -f *.o halibut core

install:
	$(INSTALL) -m 755 halibut $(bindir)/halibut
	$(MAKE) -C ../doc install prefix="$(prefix)" INSTALL="$(INSTALL)"

FORCE: # phony target to force version.o to be rebuilt every time

-include $(DEPS)

endif
