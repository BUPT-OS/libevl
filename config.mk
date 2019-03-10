# SPDX-License-Identifier: MIT

ARCH		?= $(shell uname -m | sed	\
		-e s/arm.*/arm/			\
		-e s/aarch64.*/arm64/ )
CROSS_COMPILE	?=
CC		= $(CROSS_COMPILE)gcc
LD		= $(CROSS_COMPILE)ld
AR		= $(CROSS_COMPILE)ar
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

libdir		?= lib
includedir	?= include
bindir		?= bin
testdir		?= tests

export ARCH CROSS_COMPILE CC LD AR UAPI CFLAGS LDFLAGS DESTDIR

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
  DEBUG_CPPFLAGS=
  DEBUG_CFLAGS=-g -O0
else
  DEBUG_CPPFLAGS=-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2
  DEBUG_CFLAGS=-O2
endif

BASE_CPPFLAGS := -D_GNU_SOURCE -D_REENTRANT $(DEBUG_CPPFLAGS)

BASE_CFLAGS :=	-fasynchronous-unwind-tables -pipe -fstrict-aliasing	 	\
		-Wall -Wstrict-prototypes -Wmissing-prototypes -Wno-long-long	\
		-Wno-unused-parameter -Werror $(DEBUG_CFLAGS)

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
ld-cmd = $(call run-cmd,LD,$(notdir $(1)),$(2))
ar-cmd = $(call run-cmd,AR,$(notdir $(1)),$(2) $(if $(Q),2>/dev/null))
inst-cmd = $(call run-cmd,INST,$(notdir $(1)),$(2))

MAIN_GOALS := all clean clobber mrproper install

$(TARGETS):
	$(Q)$(MAKE) -C $@ O=$(O_DIR)/$@ V=$(V)

$(O_DIR)/%.d: %.c
	$(call dep-cmd,$@,@$(CC) -MM $(CFLAGS) $< | sed 's$(comma)\($*\)\.o[ :]*$(comma)$(O_DIR)/\1.o $@: $(comma)g' > $@ || rm -f $@)

.PHONY: $(MAIN_GOALS) $(TARGETS)
