# SPDX-License-Identifier: MIT

ARCH		?= $(shell uname -m | sed	\
		-e s/arm.*/arm/			\
		-e s/aarch64.*/arm64/		\
		-e s/x86_64/x86/ )
CROSS_COMPILE	?=
CC		?= $(CROSS_COMPILE)gcc
CXX		?= $(CROSS_COMPILE)g++
LD		?= $(CROSS_COMPILE)ld
AR		?= $(CROSS_COMPILE)ar
UAPI		?= /usr/include
DESTDIR		?= /usr/evl

INSTALL		?= install
INSTALL_PROGRAM	?= $(INSTALL)
INSTALL_DATA	?= $(INSTALL) -m 644

CP		:= cp
CPIO		:= cpio
RM		:= rm
LN_S		:= ln -sf
MKDIR_P		:= mkdir -p
RMDIR_SAFE	:= rmdir --ignore-fail-on-non-empty

libdir		?= lib
includedir	?= include
bindir		?= bin
testdir		?= tests
libexecdir	?= libexec
tbitsdir	?= tidbits

export ARCH CROSS_COMPILE CC CXX LD AR UAPI CFLAGS LDFLAGS DESTDIR

MAKEFLAGS += -rR

ifneq ("$(origin O)", "command line")
  O_DIR = $(CURDIR)
else
  O_DIR := $(shell $(MKDIR_P) $(O) && cd $(O) && /bin/pwd)
endif

ifneq ("$(origin V)", "command line")
  V = 0
endif

ifeq ($(V),1)
  Q =
else
  Q = @
  MAKEFLAGS += --no-print-directory
endif

ifeq ($(D),1)
  DEBUG_CPPFLAGS=-U_FORTIFY_SOURCE
  DEBUG_CFLAGS=-g -O0
else
  DEBUG_CPPFLAGS=-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2
  DEBUG_CFLAGS=-O2
endif

BASE_CPPFLAGS := -D_GNU_SOURCE -D_REENTRANT $(DEBUG_CPPFLAGS)

GCCVER_GTE_7 := $(shell expr `${CC} -dumpversion | cut -f1 -d.` \>= 7)
ifeq "$(GCCVER_GTE_7)" "1"
	WSHADOW_FLAG="-Wshadow=local"
else
	WSHADOW_FLAG=
endif

BASE_CFLAGS :=	-pipe -fstrict-aliasing	 					\
		-Wall -Wstrict-prototypes -Wmissing-prototypes -Wno-long-long	\
		-Wno-unused-parameter ${WSHADOW_FLAG} -Werror $(DEBUG_CFLAGS)

BASE_CXXFLAGS := -pipe -fstrict-aliasing -Wall -Wno-long-long	\
		-Wno-unused-parameter ${WSHADOW_FLAG} -Werror $(DEBUG_CFLAGS)

# Easy way to hide commas in args from $(call ...) invocations
comma := ,

ifneq ($(findstring s,$(filter-out --%,$(MAKEFLAGS))),)
  quiet=y
endif

terse-echo = @$(if $(Q),echo "	$(1)	$(2)";)
define run-cmd
	$(if $(quiet),,$(call terse-echo,$(1),$(2)))
	$(Q)$(3)
endef
define run-cc
	@$(MKDIR_P) $(dir $(2))
	$(call run-cmd,$(1),$(notdir $(2)),$(3))
endef
cc-pic-cmd = $(call run-cc,CC-PIC,$(1),$(2))
cc-cmd = $(call run-cc,CC,$(1),$(2))
dep-cmd = $(call run-cc,DEP,$(1),$(2))
ccld-cmd = $(call run-cc,CCLD,$(1),$(2))
cxxld-cmd = $(call run-cc,CXXLD,$(1),$(2))
ld-cmd = $(call run-cmd,LD,$(notdir $(1)),$(2))
ar-cmd = $(call run-cmd,AR,$(notdir $(1)),$(2) $(if $(Q),2>/dev/null))
inst-cmd = $(call run-cmd,INST,$(notdir $(1)),$(2))

MAIN_GOALS := all clean clobber mrproper install install_all

_all:

# Default target when no goal was given on the command line
_all: all

$(TARGETS):
	$(Q)$(MAKE) -C $@ O=$(O_DIR)/$@ V=$(V)

$(O_DIR)/%.d: %.c
	$(call dep-cmd,$@,@$(CC) -MM $(CFLAGS) $< | sed 's$(comma)\($*\)\.o[ :]*$(comma)$(O_DIR)/\1.o $@: $(comma)g' > $@ || rm -f $@)

$(O_DIR)/%.d: %.cc
	$(call dep-cmd,$@,@$(CXX) -MM $(CXXFLAGS) $< | sed 's$(comma)\($*\)\.o[ :]*$(comma)$(O_DIR)/\1.o $@: $(comma)g' > $@ || rm -f $@)

define MAKEFLY =
# Automatically generated: do not edit

MAKEARGS := O=$(O_DIR) -C $(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) UAPI=$(UAPI) DESTDIR=$(DESTDIR)

$$(filter-out sub-make, $$(MAKECMDGOALS)): sub-make
	@:

sub-make:
	$$(MAKE) $$(MAKEARGS) $$(MAKECMDGOALS)
endef

# CURDIR is the source directory
export MAKEFLY
output-Makefile:
ifneq ($(O_DIR), $(CURDIR))
	@if test \! -e $(O_DIR)/Makefile || grep -q Automatically $(O_DIR)/Makefile; then \
		echo "$$MAKEFLY" > $(O_DIR)/Makefile; \
	fi
endif

FORCE:

.PHONY: _all $(MAIN_GOALS) output-Makefile FORCE
