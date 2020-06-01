# SPDX-License-Identifier: MIT

include ../config.mk
include ../libversion.mk

SRCFILES := $(wildcard *.c)
TARGETS = $(SRCFILES:%.c=$(O_DIR)/%)
DEPFILES = $(SRCFILES:%.c=$(O_DIR)/%.d)
TIDBITS = oob-spi oob-net-icmp

CMD_CPPFLAGS := $(BASE_CPPFLAGS) -I. -I../include -I$(O_DIR)/../include
CMD_CFLAGS := $(CMD_CPPFLAGS) $(BASE_CFLAGS)
override CFLAGS := $(CMD_CFLAGS) $(CFLAGS)

CMD_LDFLAGS := $(O_DIR)/../lib/libevl.so.$(EVL_IVERSION) -lpthread -lrt
override LDFLAGS := $(CMD_LDFLAGS) $(LDFLAGS)

$(TARGETS):

all: output-Makefile $(DEPFILES) $(TARGETS)

# Install only if install_all is given
install: all

install_all: all
	$(call inst-cmd,tidbits,			\
		for bin in $(TIDBITS); do		\
		$(INSTALL) -D $(O_DIR)/$$bin -t $(DESTDIR)/$(tbitsdir); done)

clean clobber mrproper: output-Makefile
	$(Q)$(RM) -f $(TARGETS) $(DEPFILES)

$(O_DIR)/%: %.c $(O_DIR)/%.d
	$(call ccld-cmd,$@,$(Q)$(CC) -o $(@) $< $(CFLAGS) $(LDFLAGS))

-include $(DEPFILES)
