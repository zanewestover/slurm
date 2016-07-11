# Makefile for slurmctld/xtconsumer plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = slurmctld_xtconsumer.la

slurmctld_xtconsumer_la_SOURCES = \
	xtconsumer.c

slurmctld_xtconsumer_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
slurmctld_xtconsumer_la_LIBADD = $(UTIL_LIBS)
