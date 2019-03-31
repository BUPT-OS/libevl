# SPDX-License-Identifier: MIT

include ../config.mk

SRCFILES := $(wildcard *.c)
BINARIES = $(SRCFILES:%.c=%)
TARGETS = $(SRCFILES:%.c=$(O_DIR)/%)
DEPFILES = $(SRCFILES:%.c=$(O_DIR)/%.d)
HELPERS = $(wildcard *.evl)

CMD_CPPFLAGS := $(BASE_CPPFLAGS) -I.
CMD_CFLAGS := $(CMD_CPPFLAGS) $(BASE_CFLAGS)
override CFLAGS := $(CMD_CFLAGS) $(CFLAGS)

all: $(TARGETS)

install: all
	$(call inst-cmd,commands,						\
		for bin in $(BINARIES); do					\
		$(INSTALL) -D $(O_DIR)/$$bin $(DESTDIR)/$(bindir)/$$bin; done)
	$(call inst-cmd,helpers,						\
		for hlp in $(HELPERS); do					\
		name=evl-$$(basename $$hlp .evl);				\
		$(INSTALL) -D $$hlp $(DESTDIR)/$(libexecdir)/$$name; done)

clean clobber mrproper:
	$(Q)$(RM) -f $(TARGETS) $(DEPFILES)

$(O_DIR)/%: %.c
	$(call ccld-cmd,$@,$(Q)$(CC) -o $(@) $< $(CFLAGS) $(LDFLAGS))

-include $(DEPFILES)
