CHAPTERS := $(SITE) blurb intro running input output licence manpage index

INPUTS = $(patsubst %,%.but,$(CHAPTERS))

HALIBUT = ../build/halibut

all: index.html halibut.1

index.html: $(INPUTS) $(HALIBUT)
	$(HALIBUT) --text=halibut.txt --html --info=halibut.info \
		--ps=halibut.ps --pdf=halibut.pdf $(INPUTS)

halibut.1: manpage.but
	$(HALIBUT) --man=halibut.1 manpage.but

clean:
	rm -f *.html *.txt *.hlp *.cnt *.1 *.info*
