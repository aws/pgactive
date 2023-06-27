bdr_subdir = doc
bdr_top_builddir = ..
include $(bdr_top_builddir)/Makefile.global

ifndef JADE
JADE = $(missing) jade
endif

SGMLINCLUDE = -D . -D ${srcdir}

SPFLAGS += -wall -wno-unused-param -wno-empty -wfully-tagged

JADE.html.call = $(JADE) $(JADEFLAGS) $(SPFLAGS) $(SGMLINCLUDE) $(CATALOG) -d stylesheet.dsl -t sgml -i output-html

ALLSGML := $(wildcard $(srcdir)/*.sgml)
# to build bookindex
ALMOSTALLSGML := $(filter-out %bookindex.sgml,$(ALLSGML))
GENERATED_SGML = version.sgml bookindex.sgml

Makefile: Makefile.in
	cd $(bdr_top_builddir) && ./config.status doc/Makefile

all: html

html: html-stamp

html-stamp: bdr.sgml $(ALLSGML) $(GENERATED_SGML) stylesheet.dsl website-docs.css
	$(MKDIR_P) html
	$(JADE.html.call) -i include-index $<
	cp $(srcdir)/stylesheet.css $(srcdir)/website-docs.css html/
	touch $@

version.sgml: ${bdr_top_builddir}/bdr_config_generated.h
	{ \
	  echo "<!ENTITY bdrversion \"$(BDR_VERSION)\">"; \
	} > $@

HTML.index: bdr.sgml $(ALMOSTALLSGML) stylesheet.dsl
	@$(MKDIR_P) html
	$(JADE.html.call) -V html-index $<

website-docs.css:
	@$(MKDIR_P) html
	curl http://www.postgresql.org/media/css/docs.css > ${srcdir}/website-docs.css

bookindex.sgml: HTML.index
ifdef COLLATEINDEX
	LC_ALL=C $(PERL) $(COLLATEINDEX) -f -g -i 'bookindex' -o $@ $<
else
	@$(missing) collateindex.pl $< $@
endif

clean:
	rm -f html-stamp
	rm -f HTML.index $(GENERATED_SGML)

maintainer-clean:
	rm -rf html
	rm -rf Makefile

zip: html
	cp -r html bdr-docs-$(BDR_VERSION)
	zip -r bdr-docs-$(BDR_VERSION).zip bdr-docs-$(BDR_VERSION)
	rm -rf bdr-docs-$(BDR_VERSION)

install: html
	@$(MKDIR_P) $(DESTDIR)$(docdir)/$(docmoduledir)/bdr
	@$(INSTALL_DATA) $(wildcard html/*.html) $(wildcard html/*.css) $(DESTDIR)$(docdir)/$(docmoduledir)/bdr
	@echo Installed docs to $(DESTDIR)$(docdir)/$(docmoduledir)/bdr

.PHONY: html all
