# Buttress master makefile

# Requires a compiler with -MD support, currently

# `make' from top level will build in buttress.b
# `make BUILDDIR=foo' from top level will build in directory foo
ifndef REALBUILD
ifndef BUILDDIR
BUILDDIR := build
endif
all:
	@test -d $(BUILDDIR) || mkdir $(BUILDDIR)
	@make -C $(BUILDDIR) -f ../Makefile REALBUILD=yes
clean:
	@test -d $(BUILDDIR) || mkdir $(BUILDDIR)
	@make -C $(BUILDDIR) -f ../Makefile clean REALBUILD=yes
else

# The `real' makefile part.

CFLAGS += -Wall -W

ifdef LOGALLOC
CFLAGS += -DLOGALLOC
endif

ifdef RELEASE
ifndef VERSION
VERSION := $(RELEASE)
endif
else
CFLAGS += -g
endif

ifndef VER
ifdef VERSION
VER := $(VERSION)
endif
endif
ifdef VER
VDEF := -DVERSION=\"$(VER)\"
endif

SRC := ../

MODULES := main malloc ustring error help licence version misc tree23
MODULES += input keywords contents index style biblio

OBJECTS := $(addsuffix .o,$(MODULES))
DEPS := $(addsuffix .d,$(MODULES))

buttress: $(OBJECTS)
	$(CC) $(LFLAGS) -o buttress $(OBJECTS)

%.o: $(SRC)%.c
	$(CC) $(CFLAGS) -MD -c $<

version.o: FORCE
	$(CC) $(VDEF) -MD -c $(SRC)version.c

clean::
	rm -f *.o buttress core

FORCE: # phony target to force version.o to be rebuilt every time

-include $(DEPS)

endif
