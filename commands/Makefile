include ../scripts/config.mk

CMDFILES := $(wildcard *.c)
BINARIES = $(CMDFILES:%.c=%)
TARGETS = $(CMDFILES:%.c=$(O_DIR)/%)
DEPFILES = $(CMDFILES:%.c=$(O_DIR)/%.d)

CMD_CPPFLAGS := $(BASE_CPPFLAGS) -I.
CMD_CFLAGS := $(CMD_CPPFLAGS) $(BASE_CFLAGS)
override CFLAGS := $(CMD_CFLAGS) $(CFLAGS)

all: $(TARGETS)

install: all
	$(Q)for bin in $(BINARIES); do						\
		$(INSTALL) -D $(O_DIR)/$$bin $(DESTDIR)/$(bindir)/$$bin;	\
	done

clean clobber mrproper:
	$(Q)$(RM) -f $(TARGETS) $(DEPFILES)

$(O_DIR)/%: %.c
	$(call ccld-cmd,$@,$(Q)$(CC) -o $(@) $< $(CFLAGS) $(LDFLAGS))

.PHONY: all install clean clobber mrproper

-include $(DEPFILES)
