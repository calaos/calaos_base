if CALAOS_BUILD_THEME
MAINTAINERCLEANFILES = Makefile.in

EDJE_CC = edje_cc

EDJE_FLAGS = \
-id $(top_srcdir)/data/themes/widgets/note/images \
-fd $(top_srcdir)/data/themes/default/fonts

EDJE_VERB = $(EDJE_VERB_$(V))
EDJE_VERB_ = $(EDJE_VERB_$(AM_DEFAULT_VERBOSITY))
EDJE_VERB_1 = -v

defaultdir = $(pkgdatadir)/widgets/note
icondir = $(pkgdatadir)/widgets/note

default_DATA = default.edj
icon_DATA = icon.edj

IMG = \
images/icon_widget_notes.png \
images/note_back.png

EDC = \
	main.edc \
	icon.edc

EXTRA_DIST = \
  $(IMG) \
  $(EDC)

default.edj: Makefile $(EXTRA_DIST)
	$(AM_V_GEN)$(EDJE_CC) $(EDJE_VERB) $(EDJE_FLAGS) \
	$(top_srcdir)/data/themes/widgets/note/main.edc \
	$(top_builddir)/data/themes/widgets/note/default.edj

icon.edj: Makefile $(EXTRA_DIST)
	$(AM_V_GEN)$(EDJE_CC) $(EDJE_VERB) $(EDJE_FLAGS) \
	$(top_srcdir)/data/themes/widgets/note/icon.edc \
	$(top_builddir)/data/themes/widgets/note/icon.edj

clean-local:
	rm -f *.edj
endif
