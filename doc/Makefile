# programs for documentation
DDOC		:= /usr/share/sgml/debiandoc/dtd/sgml/1.0/debiandoc.dcl
DEBIANDOC2HTML	:= debiandoc2html -d $(DDOC) -l C -c 
# detailed sgml validation
NSGMLS		:= nsgmls -ges -wall $(DDOC)

all: clean
	$(NSGMLS) yaboot-howto.sgml
	$(DEBIANDOC2HTML) yaboot-howto.sgml
	ln -s index.en.html yaboot-howto.html/index.html

clean:
	-rm -rf yaboot-howto.html
