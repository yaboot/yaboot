# programs for documentation
DDOC		:= /usr/share/sgml/debiandoc/dtd/sgml/1.0/debiandoc.dcl
DEBIANDOC2HTML	:= debiandoc2html -d $(DDOC) -c
# detailed sgml validation
NSGMLS		:= nsgmls -ges -wall $(DDOC)

all: clean
	$(NSGMLS) yaboot-howto.sgml
	$(NSGMLS) yaboot-howto.de.sgml
	$(DEBIANDOC2HTML) -l en yaboot-howto.sgml
	$(DEBIANDOC2HTML) -l de -byaboot-howto yaboot-howto.de.sgml
	ln -s index.en.html yaboot-howto.html/index.html

clean:
	-rm -rf yaboot-howto.html
