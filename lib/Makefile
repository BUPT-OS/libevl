# SPDX-License-Identifier: MIT

include ../config.mk
include ../libversion.mk

SONAME := libevl.so
DTSONAME := $(SONAME).$(EVL_IVERSION)
SOLIBNAME := libevl-$(EVL_SERIAL).so.$(EVL_IVERSION)
ARLIBNAME := libevl-$(EVL_SERIAL).a
TARGETS := $(O_DIR)/$(SOLIBNAME) $(O_DIR)/$(ARLIBNAME)

SRCFILES := $(wildcard *.c arch/$(ARCH)/*.c)
PIC_OBJFILES = $(SRCFILES:%.c=$(O_DIR)/%-pic.o)
OBJFILES = $(SRCFILES:%.c=$(O_DIR)/%.o)
DEPFILES = $(SRCFILES:%.c=$(O_DIR)/%.d)

LIB_CPPFLAGS :=  $(BASE_CPPFLAGS)		\
		 -I.				\
		 -I../include			\
		 -Iarch/$(ARCH)/include		\
		 -I$(O_DIR)/../include		\
		 -I$(O_DIR)

LIB_CFLAGS := $(LIB_CPPFLAGS) $(BASE_CFLAGS)

override CFLAGS := $(LIB_CFLAGS) $(CFLAGS)

override LDFLAGS := $(LDFLAGS) -lpthread -lrt

all: output-Makefile $(TARGETS)

install: all
	$(call inst-cmd,$(SOLIBNAME),$(INSTALL) -D $(O_DIR)/$(SOLIBNAME) $(DESTDIR)/$(libdir)/$(SOLIBNAME))
	@$(LN_S) $(SOLIBNAME) $(DESTDIR)/$(libdir)/$(DTSONAME)
	@$(LN_S) $(DTSONAME) $(DESTDIR)/$(libdir)/$(SONAME)
	$(call inst-cmd,$(ARLIBNAME),$(INSTALL) -D $(O_DIR)/$(ARLIBNAME) $(DESTDIR)/$(libdir)/$(ARLIBNAME))

clean clobber mrproper: output-Makefile
	$(Q)$(RM) -f $(O_DIR)/git-stamp.h $(PIC_OBJFILES) $(OBJFILES) \
		$(TARGETS) $(O_DIR)/$(DTSONAME) $(DEPFILES)

$(O_DIR)/$(SOLIBNAME): $(PIC_OBJFILES)
	$(call ld-cmd,$@,$(CC) -shared -Wl$(comma)-soname$(comma)$(DTSONAME) -o $(@) \
		$(PIC_OBJFILES) $(LDFLAGS) -Wl$(comma)-rpath=$(libdir) -Wl$(comma)--export-dynamic)
	$(Q)$(LN_S) $(SOLIBNAME) $(O_DIR)/$(DTSONAME)

$(O_DIR)/$(ARLIBNAME): $(OBJFILES)
	$(call ar-cmd,$@,$(AR) ru $@ $(OBJFILES))

$(O_DIR)/syscall-pic.o $(O_DIR)/syscall.o: override CFLAGS := $(CFLAGS) -fomit-frame-pointer

$(O_DIR)/version-pic.o $(O_DIR)/version.o: override CFLAGS := $(CFLAGS) -DLIBSERIAL=\"$(EVL_SERIAL)\"
version.c: $(O_DIR)/git-stamp.h

$(O_DIR)/git-stamp.h: git-stamp
	@if test -r ../.git; then							\
	  stamp=`git --git-dir=../.git rev-list --abbrev-commit -1 HEAD`;		\
	  if test \! -s $@ || grep -wvq $$stamp $@; then				\
		date=`git --git-dir=../.git log -1 $$stamp --pretty=format:%ci`;	\
		echo "#define GIT_STAMP \"#$$stamp ($$date)\"" > $@;			\
	  fi;										\
	elif test \! -r $@ -o -s $@; then						\
	    $(RM) -f $@ && touch $@;							\
	fi; true

.PHONY: git-stamp

$(O_DIR)/%-pic.o: %.c
	$(call cc-pic-cmd,$@,$(CC) $(CFLAGS) -fPIC -c -o $@ $<)

$(O_DIR)/%.o: %.c
	$(call cc-cmd,$@,$(CC) $(CFLAGS) -c -o $@ $<)

-include $(DEPFILES)
