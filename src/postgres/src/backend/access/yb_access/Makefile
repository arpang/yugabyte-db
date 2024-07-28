#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for access/yb_access
#
# IDENTIFICATION
#    src/backend/access/yb_access/Makefile
#
#-------------------------------------------------------------------------

subdir = src/backend/access/yb_access
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global

OBJS = \
	yb_scan.o \
	yb_lsm.o \
	yb_pg_inherits_scan.o \
	yb_cache_table_am_handler.o \
	ybdocdbam_handler.o

include $(top_srcdir)/src/backend/common.mk
