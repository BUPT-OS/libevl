# SPDX-License-Identifier: MIT

include ../config.mk
include ../libversion.mk

ALLSRC := $(wildcard *.c)
HELPSRC := helpers.c
HELPOBJ := $(O_DIR)/helpers.o
TESTSRC := $(filter-out $(HELPSRC),$(ALLSRC))
TARGETS = $(TESTSRC:%.c=$(O_DIR)/%)
DEPFILES = $(ALLSRC:%.c=$(O_DIR)/%.d)

TEST_CPPFLAGS := $(BASE_CPPFLAGS)	\
		 -I.			\
		 -I../include		\
		 -I$(O_DIR)/../include

TEST_CFLAGS := $(TEST_CPPFLAGS) $(BASE_CFLAGS)
override CFLAGS := $(TEST_CFLAGS) $(CFLAGS)

TEST_LDFLAGS := $(O_DIR)/../lib/libevenless.so.$(EVL_IVERSION) -lpthread -lrt
override LDFLAGS := $(TEST_LDFLAGS) $(LDFLAGS)

all: $(TARGETS)

$(TARGETS): $(HELPOBJ)

install: all
	$(Q)for bin in $(TESTSRC:%.c=%); do					\
		$(INSTALL) -D $(O_DIR)/$$bin $(DESTDIR)/$(testdir)/$$bin;	\
	done

clean clobber mrproper:
	$(Q)$(RM) -f $(TARGETS) $(DEPFILES) $(HELPOBJ)

$(O_DIR)/%.o: %.c
	$(call cc-cmd,$@,$(Q)$(CC) -o $(@) $< -c $(CFLAGS))

$(O_DIR)/%: %.c
	$(call ccld-cmd,$@,$(Q)$(CC) -o $(@) $< $(HELPOBJ) $(CFLAGS) $(LDFLAGS))

-include $(DEPFILES)
