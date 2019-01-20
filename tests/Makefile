# SPDX-License-Identifier: MIT

include ../config.mk
include ../libversion.mk

TESTFILES := $(wildcard *.c)
BINARIES = $(TESTFILES:%.c=%)
TARGETS = $(TESTFILES:%.c=$(O_DIR)/%)
DEPFILES = $(TESTFILES:%.c=$(O_DIR)/%.d)

TEST_CPPFLAGS := $(BASE_CPPFLAGS)		\
		 -I.				\
		 -I../include			\
		 -I$(O_DIR)/../include

TEST_CFLAGS := $(TEST_CPPFLAGS) $(BASE_CFLAGS)
override CFLAGS := $(TEST_CFLAGS) $(CFLAGS)

TEST_LDFLAGS := $(O_DIR)/../lib/libevenless.so.$(EVL_IVERSION) -lpthread -lrt
override LDFLAGS := $(TEST_LDFLAGS) $(LDFLAGS)

all: $(TARGETS)

install: all
	$(Q)for bin in $(BINARIES); do						\
		$(INSTALL) -D $(O_DIR)/$$bin $(DESTDIR)/$(bindir)/$$bin;	\
	done

clean clobber mrproper:
	$(Q)$(RM) -f $(TARGETS) $(DEPFILES)

$(O_DIR)/%: %.c
	$(call ccld-cmd,$@,$(Q)$(CC) -o $(@) $< $(CFLAGS) $(LDFLAGS))

-include $(DEPFILES)
