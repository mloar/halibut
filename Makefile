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

all:
	@test -d $(BUILDDIR) || mkdir $(BUILDDIR)
	@make -C $(BUILDDIR) -f ../Makefile REALBUILD=yes

spotless: topclean
	@test -d $(BUILDDIR) || mkdir $(BUILDDIR)
	@make -C $(BUILDDIR) -f ../Makefile spotless REALBUILD=yes

clean: topclean
	@test -d $(BUILDDIR) || mkdir $(BUILDDIR)
	@make -C $(BUILDDIR) -f ../Makefile clean REALBUILD=yes

# Remove Halibut output files in the source directory (may
# have been created by running, for example, `build/halibut
# inputs/test.but').
topclean:
	rm -f *.html output.* *.tar.gz

# Make a release archive. If $(VERSION) is specified, this will
# also contain a `manifest' file which will be used to decide the
# version number automatically.
release:
	find . -name CVS -prune -o -name build -prune -o -name reltmp -prune \
	       -o -type d -exec mkdir -p reltmp/$(RELDIR)/{} \;
	find . -name CVS -prune -o -name build -prune -o -name reltmp -prune \
	       -o -name '*.orig' -prune -o -name '*.rej' -prune \
	       -o -name '*.txt' -prune -o -name '*.html' -prune \
	       -o -name '*.1' -prune -o -name '.cvsignore' -prune \
	       -o -name '*.gz' -prune -o -name '.[^.]*' -prune \
	       -o -type f -exec ln -s $(PWD)/{} reltmp/$(RELDIR)/{} \;
	if test "x$(VERSION)y" != "xy"; then                            \
	    (cd reltmp/$(RELDIR);                                       \
	     find . -name '*.[ch]' -exec md5sum {} \;                   \
	    ) > reltmp/$(RELDIR)/manifest;                              \
	    echo "-DVERSION=\"$(VERSION)\"" > reltmp/$(RELDIR)/version; \
	fi
	tar chzvCf reltmp - $(RELDIR) > $(RELDIR).tar.gz
	rm -rf reltmp

else

# The `real' makefile part.

CFLAGS += -Wall -W

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
VDEF = `(cd $(SRC); md5sum -c manifest && cat version)`
endif

halibut:

SRC := ../

LIBCHARSET_SRCDIR = $(SRC)charset/
LIBCHARSET_OBJDIR = ./#
LIBCHARSET_OBJPFX = cs-#
LIBCHARSET_GENPFX = charset-#
MD = -MD
CFLAGS += -I$(LIBCHARSET_SRCDIR) -I$(LIBCHARSET_OBJDIR)
include $(LIBCHARSET_SRCDIR)Makefile

MODULES := main malloc ustring error help licence version misc tree234
MODULES += input keywords contents index style biblio
MODULES += bk_text bk_xhtml bk_whlp bk_man bk_info bk_paper bk_ps bk_pdf
MODULES += winhelp psdata

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

FORCE: # phony target to force version.o to be rebuilt every time

-include $(DEPS)

endif
