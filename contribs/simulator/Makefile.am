#
# Makefile for Slurm workload simulator package
#

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = list_trace rsv_trace_builder trace_builder sim_ctlr sim_mgr update_trace

list_trace_SOURCES = list_trace.c sim_trace.h
rsv_trace_builder_SOURCES = rsv_trace_builder.c sim_trace.h
sim_ctlr_SOURCES = sim_ctlr.c
sim_mgr_SOURCES = sim_mgr.c comm_protocol.h sim_trace.h slurm_sim.h
trace_builder_SOURCES = trace_builder.c sim_trace.h
update_trace_SOURCES = update_trace.c sim_trace.h

convenience_libs = $(top_builddir)/src/api/libslurm.o 

list_trace_LDADD = $(convenience_libs)
rsv_trace_builder_LDADD = $(convenience_libs)
sim_ctlr_LDADD = $(convenience_libs)
sim_mgr_LDADD = $(convenience_libs)
trace_builder_LDADD = $(convenience_libs)
update_trace_LDADD = $(convenience_libs)

list_trace_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) -ldl -lrt -lpthread -lhwloc
rsv_trace_builder_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) -ldl -lrt -lpthread -lhwloc
sim_ctlr_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) -ldl -lrt -lpthread -lhwloc
sim_mgr_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) -ldl -lrt -lpthread -lhwloc
trace_builder_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) -ldl -lrt -lpthread -lhwloc
update_trace_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) -ldl -lrt -lpthread -lhwloc

pkglib_LTLIBRARIES = libslurm_sim.la

libslurm_sim_la_SOURCES = sim_lib.c comm_protocol.h slurm_sim.h
libslurm_sim_la_LDFLAGS = $(SO_LDFLAGS) -ldl -lrt -lpthread -lhwloc

force:
$(convenience_libs) : force
	      @cd `dirname $@` && $(MAKE) `basename $@`
