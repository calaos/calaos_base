if CALAOS_BUILD_THEME
MAINTAINERCLEANFILES = Makefile.in

EDJE_CC = edje_cc

EDJE_FLAGS = \
-id $(top_srcdir)/data/themes/widgets/clock/images \
-fd $(top_srcdir)/data/themes/default/fonts

EDJE_VERB = $(EDJE_VERB_$(V))
EDJE_VERB_ = $(EDJE_VERB_$(AM_DEFAULT_VERBOSITY))
EDJE_VERB_1 = -v

defaultdir = $(pkgdatadir)/widgets/clock
icondir = $(pkgdatadir)/widgets/clock

default_DATA = default.edj
icon_DATA = icon.edj

IMG = \
images/clock_min_49.png \
images/clock_hour_34.png \
images/clock_min_23.png \
images/clock_min_54.png \
images/clock_sec_02.png \
images/clock_min_42.png \
images/clock_hour_20.png \
images/clock_hour_43.png \
images/clock_sec_38.png \
images/clock_min_00.png \
images/clock_hour_48.png \
images/clock_min_03.png \
images/clock_min_56.png \
images/clock_min_55.png \
images/clock_min_05.png \
images/clock_sec_06.png \
images/clock_min_11.png \
images/clock_sec_18.png \
images/clock_min_06.png \
images/clock_sec_03.png \
images/clock_sec_22.png \
images/clock_min_48.png \
images/clock_min_38.png \
images/clock_hour_58.png \
images/icon_widget_clock.png \
images/clock_sec_36.png \
images/clock_hour_03.png \
images/clock_hour_29.png \
images/clock_sec_28.png \
images/clock_hour_39.png \
images/clock_hour_42.png \
images/clock_sec_58.png \
images/clock_sec_05.png \
images/clock_hour_56.png \
images/clock_hour_14.png \
images/clock_hour_04.png \
images/clock_hour_08.png \
images/clock_hour_45.png \
images/clock_sec_45.png \
images/clock_min_46.png \
images/clock_sec_41.png \
images/clock_hour_57.png \
images/clock_sec_33.png \
images/clock_min_07.png \
images/clock_sec_50.png \
images/clock_min_51.png \
images/clock_hour_27.png \
images/clock_min_12.png \
images/clock_sec_12.png \
images/clock_hour_33.png \
images/clock_sec_55.png \
images/clock_sec_42.png \
images/clock_hour_59.png \
images/clock_hour_24.png \
images/clock_hour_23.png \
images/clock_min_52.png \
images/clock_hour_35.png \
images/clock_sec_11.png \
images/clock_sec_14.png \
images/clock_sec_08.png \
images/clock_min_30.png \
images/clock_hour_00.png \
images/clock_hour_46.png \
images/clock_sec_46.png \
images/clock_sec_56.png \
images/clock_hour_09.png \
images/clock_sec_34.png \
images/clock_sec_52.png \
images/clock_min_02.png \
images/clock_min_21.png \
images/clock_min_04.png \
images/clock_hour_28.png \
images/clock_min_22.png \
images/clock_min_36.png \
images/clock_sec_19.png \
images/clock_min_33.png \
images/clock_hour_51.png \
images/clock_sec_16.png \
images/clock_min_59.png \
images/clock_min_31.png \
images/clock_hour_22.png \
images/clock_sec_04.png \
images/clock_min_27.png \
images/clock_min_29.png \
images/clock_sec_49.png \
images/clock_hour_52.png \
images/clock_min_15.png \
images/clock_hour_10.png \
images/clock_sec_09.png \
images/clock_min_44.png \
images/clock_sec_39.png \
images/clock_hour_49.png \
images/clock_hour_18.png \
images/clock_min_19.png \
images/clock_min_13.png \
images/clock_hour_40.png \
images/clock_hour_15.png \
images/clock_min_24.png \
images/clock_hour_13.png \
images/clock_sec_10.png \
images/clock_sec_00.png \
images/clock_hour_19.png \
images/clock_hour_31.png \
images/clock_sec_20.png \
images/clock_hour_30.png \
images/clock_sec_57.png \
images/clock_min_34.png \
images/clock_sec_54.png \
images/clock_hour_41.png \
images/clock_sec_23.png \
images/clock_hour_05.png \
images/clock_hour_17.png \
images/clock_min_41.png \
images/clock_sec_59.png \
images/clock_hour_38.png \
images/clock_hour_36.png \
images/clock_sec_48.png \
images/clock_min_43.png \
images/clock_min_17.png \
images/clock_min_01.png \
images/clock_hour_53.png \
images/clock_min_26.png \
images/clock_min_39.png \
images/clock_min_09.png \
images/clock_sec_07.png \
images/clock_sec_13.png \
images/clock_hour_26.png \
images/clock_min_28.png \
images/clock_hour_37.png \
images/clock_min_14.png \
images/clock_min_57.png \
images/clock_sec_15.png \
images/widget_clock_back.png \
images/clock_sec_32.png \
images/clock_sec_01.png \
images/clock_min_32.png \
images/clock_hour_55.png \
images/clock_sec_21.png \
images/clock_hour_47.png \
images/clock_sec_24.png \
images/clock_hour_32.png \
images/clock_hour_01.png \
images/clock_min_25.png \
images/clock_min_53.png \
images/clock_hour_21.png \
images/clock_min_58.png \
images/clock_min_50.png \
images/clock_sec_31.png \
images/clock_min_20.png \
images/clock_sec_44.png \
images/clock_sec_17.png \
images/clock_hour_12.png \
images/clock_min_35.png \
images/clock_min_16.png \
images/clock_min_47.png \
images/clock_min_10.png \
images/clock_sec_40.png \
images/clock_hour_11.png \
images/clock_hour_02.png \
images/clock_hour_44.png \
images/clock_hour_06.png \
images/clock_sec_29.png \
images/clock_min_18.png \
images/clock_sec_43.png \
images/clock_hour_16.png \
images/clock_hour_50.png \
images/clock_min_45.png \
images/clock_sec_53.png \
images/clock_sec_47.png \
images/clock_sec_25.png \
images/clock_hour_25.png \
images/clock_sec_30.png \
images/clock_sec_51.png \
images/clock_sec_37.png \
images/clock_min_40.png \
images/clock_sec_27.png \
images/clock_hour_54.png \
images/clock_min_08.png \
images/clock_min_37.png \
images/clock_hour_07.png \
images/clock_sec_26.png \
images/clock_sec_35.png

EDC = \
	main.edc \
	icon.edc \
	images.edc \
	widget_clock.edc \
	widget_clock_needle.edc \
	widget_clock_prog.edc

EXTRA_DIST = \
  $(IMG) \
  $(EDC)

default.edj: Makefile $(EXTRA_DIST)
	$(AM_V_GEN)$(EDJE_CC) $(EDJE_VERB) $(EDJE_FLAGS) \
	$(top_srcdir)/data/themes/widgets/clock/main.edc \
	$(top_builddir)/data/themes/widgets/clock/default.edj

icon.edj: Makefile $(EXTRA_DIST)
	$(AM_V_GEN)$(EDJE_CC) $(EDJE_VERB) $(EDJE_FLAGS) \
	$(top_srcdir)/data/themes/widgets/clock/icon.edc \
	$(top_builddir)/data/themes/widgets/clock/icon.edj

clean-local:
	rm -f *.edj
endif
