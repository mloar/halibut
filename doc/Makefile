CHAPTERS := $(SITE) blurb intro running input output licence index

INPUTS = $(patsubst %,%.but,$(CHAPTERS))

HALIBUT = ../build/halibut

all: Contents.html

Contents.html: $(INPUTS) $(HALIBUT)
	$(HALIBUT) $(INPUTS)
	rm -f index.html
	ln -s Contents.html index.html
	mv output.txt halibut.txt
	rm -f output.hlp output.cnt output.1

clean:
	rm -f *.html *.txt *.hlp *.cnt *.1
