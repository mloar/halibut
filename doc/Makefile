CHAPTERS := $(SITE) blurb intro running input output licence index

INPUTS = $(patsubst %,%.but,$(CHAPTERS))

HALIBUT = ../build/halibut

all: Contents.html

Contents.html: $(INPUTS) $(HALIBUT)
	$(HALIBUT) --text=halibut.txt --html $(INPUTS)

clean:
	rm -f *.html *.txt *.hlp *.cnt *.1
