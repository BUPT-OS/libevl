# SPDX-License-Identifier: MIT

include ../config.mk
include ../libversion.mk

SONAME := libeshi.so
DTSONAME := $(SONAME).$(EVL_IVERSION)
SOLIBNAME := libeshi-$(EVL_SERIAL).so.$(EVL_IVERSION)
ARLIBNAME := libeshi-$(EVL_SERIAL).a
TARGETS := $(O_DIR)/$(SOLIBNAME) $(O_DIR)/$(ARLIBNAME)

SRCFILES := $(wildcard *.c)
PIC_OBJFILES = $(SRCFILES:%.c=$(O_DIR)/%-pic.o)
OBJFILES = $(SRCFILES:%.c=$(O_DIR)/%.o)
DEPFILES = $(SRCFILES:%.c=$(O_DIR)/%.d)

LIB_CPPFLAGS :=  $(BASE_CPPFLAGS)		\
		 -I.				\
		 -I../include/eshi		\
		 -I../include

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
	$(Q)$(RM) -f $(PIC_OBJFILES) $(OBJFILES) $(TARGETS) $(O_DIR)/$(DTSONAME) $(DEPFILES)

$(O_DIR)/$(SOLIBNAME): $(PIC_OBJFILES)
	$(call ld-cmd,$@,$(CC) -shared -Wl$(comma)-soname$(comma)$(DTSONAME) -o $(@) \
		$(PIC_OBJFILES) $(LDFLAGS) -Wl$(comma)-rpath=$(libdir) -Wl$(comma)--export-dynamic)
	$(Q)$(LN_S) $(SOLIBNAME) $(O_DIR)/$(DTSONAME)

$(O_DIR)/$(ARLIBNAME): $(OBJFILES)
	$(call ar-cmd,$@,$(AR) ru $@ $(OBJFILES))

$(O_DIR)/%-pic.o: %.c
	$(call cc-pic-cmd,$@,$(CC) $(CFLAGS) -fPIC -c -o $@ $<)

$(O_DIR)/%.o: %.c
	$(call cc-cmd,$@,$(CC) $(CFLAGS) -c -o $@ $<)

-include $(DEPFILES)
