# SPDX-License-Identifier: MIT

include ../config.mk
include ../libversion.mk

UTILFILES := $(wildcard *.c)
BINARIES = $(UTILFILES:%.c=%)
TARGETS = $(UTILFILES:%.c=$(O_DIR)/%)
DEPFILES = $(UTILFILES:%.c=$(O_DIR)/%.d)

UTIL_CPPFLAGS := $(BASE_CPPFLAGS)		\
		 -I.				\
		 -I../include			\
		 -I$(O_DIR)/../include

UTIL_CFLAGS := $(UTIL_CPPFLAGS) $(BASE_CFLAGS)
override CFLAGS := $(UTIL_CFLAGS) $(CFLAGS)

UTIL_LDFLAGS := $(O_DIR)/../lib/libevl.so.$(EVL_IVERSION) -lpthread -lrt
override LDFLAGS := $(UTIL_LDFLAGS) $(LDFLAGS)

all: output-Makefile $(TARGETS)

install: all
	$(call inst-cmd,benchmarks,$(Q)for bin in $(BINARIES); do		\
		$(INSTALL) -D $(O_DIR)/$$bin $(DESTDIR)/$(bindir)/$$bin;	\
	done)

clean clobber mrproper: output-Makefile
	$(Q)$(RM) -f $(TARGETS) $(DEPFILES)

$(O_DIR)/%: %.c
	$(call ccld-cmd,$@,$(Q)$(CC) -o $(@) $< $(CFLAGS) $(LDFLAGS))

-include $(DEPFILES)
