CHAPTERS := $(SITE) blurb intro running input output licence manpage index

INPUTS = $(patsubst %,%.but,$(CHAPTERS))

HALIBUT = ../build/halibut

all: Contents.html halibut.1

Contents.html: $(INPUTS) $(HALIBUT)
	$(HALIBUT) --text=halibut.txt --html $(INPUTS)

halibut.1: manpage.but
	$(HALIBUT) --man=halibut.1 manpage.but

clean:
	rm -f *.html *.txt *.hlp *.cnt *.1
