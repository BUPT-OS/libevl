# SPDX-License-Identifier: MIT

include ../config.mk
include ../libversion.mk

ALLSRC := $(wildcard *.c)
HELPSRC := helpers.c
HELPOBJ := $(O_DIR)/helpers.o
HELPOBJ_ESHI := $(O_DIR)/helpers-eshi.o
TESTSRC := $(filter-out $(HELPSRC),$(ALLSRC))
FPSRC := fault.c
ESHISRC := $(shell cat eshi.list)
TARGETS = $(TESTSRC:%.c=$(O_DIR)/%) $(ESHISRC:%.c=$(O_DIR)/%.eshi)
NEEDLIBM = $(FPSRC:%.c=$(O_DIR)/%)
DEPFILES = $(ALLSRC:%.c=$(O_DIR)/%.d)

TEST_CPPFLAGS := $(BASE_CPPFLAGS)	\
		 -I.			\
		 -I../include		\
		 -I$(O_DIR)/../include
TEST_CFLAGS := $(TEST_CPPFLAGS) $(BASE_CFLAGS)
override CFLAGS := $(TEST_CFLAGS) $(CFLAGS)

# FIXME: Temporarily use static linking
TEST_LDFLAGS := $(O_DIR)/../lib/libevl-$(EVL_IVERSION).a -lpthread -lrt -static $(LDFLAGS)
$(NEEDLIBM): override TEST_LDFLAGS += -lm
ESHI_LDFLAGS := $(O_DIR)/../eshi/libeshi.so.$(EVL_IVERSION) -lpthread -lrt $(LDFLAGS)

ESHI_CPPFLAGS := $(BASE_CPPFLAGS)	\
		 -D__ESHI__		\
		 -I.			\
		 -I../include/eshi	\
		 -I../include		\
		 -I$(O_DIR)/../include
ESHI_CFLAGS := $(ESHI_CPPFLAGS) $(BASE_CFLAGS)

all: output-Makefile $(DEPFILES) $(TARGETS)
		$(Q)$(MAKE) -C compile-tests O=$(O_DIR)/compile-tests V=$(V) $@ || exit 1

$(TARGETS): $(HELPOBJ) $(HELPOBJ_ESHI)

install install_all: all
	$(call inst-cmd,tests,$(Q)for bin in $(TESTSRC:%.c=%); do		\
		$(INSTALL) -D $(O_DIR)/$$bin -t $(DESTDIR)/$(testdir);		\
	done)
	$(call inst-cmd,tests-eshi,$(Q)for bin in $(ESHISRC:%.c=%.eshi); do	\
		$(INSTALL) -D $(O_DIR)/$$bin -t $(DESTDIR)/$(testdir)/eshi;	\
	done)
	$(Q)$(MAKE) -C compile-tests O=$(O_DIR)/compile-tests V=$(V) $@ || exit 1

clean clobber mrproper:
	$(Q)$(RM) -f $(TARGETS) $(DEPFILES) $(HELPOBJ)
	$(Q)$(MAKE) -C compile-tests O=$(O_DIR)/compile-tests V=$(V) $@ || exit 1

$(HELPOBJ_ESHI): $(HELPSRC)
	$(call cc-cmd,$@,$(Q)$(CC) -o $(@) $< -c $(ESHI_CFLAGS))

$(O_DIR)/%.o: %.c $(O_DIR)/%.d
	$(call cc-cmd,$@,$(Q)$(CC) -o $(@) $< -c $(CFLAGS))

$(O_DIR)/%: %.c $(O_DIR)/%.d
	$(call ccld-cmd,$@,$(Q)$(CC) -o $(@) $< $(HELPOBJ) $(TEST_CFLAGS) $(TEST_LDFLAGS))

$(O_DIR)/%.eshi: %.c $(O_DIR)/%.d
	$(call ccld-cmd,$@,$(Q)$(CC) -o $(@) $< $(HELPOBJ_ESHI) $(ESHI_CFLAGS) $(ESHI_LDFLAGS))

-include $(DEPFILES)
