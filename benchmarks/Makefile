# SPDX-License-Identifier: MIT

include ../config.mk
include ../libversion.mk

BENCHFILES := $(wildcard *.c)
BINARIES = $(BENCHFILES:%.c=%)
TARGETS = $(BENCHFILES:%.c=$(O_DIR)/%)
DEPFILES = $(BENCHFILES:%.c=$(O_DIR)/%.d)

BENCH_CPPFLAGS := $(BASE_CPPFLAGS)		\
		 -I.				\
		 -I../include			\
		 -I$(O_DIR)/../include

BENCH_CFLAGS := $(BENCH_CPPFLAGS) $(BASE_CFLAGS)
override CFLAGS := $(BENCH_CFLAGS) $(CFLAGS)

BENCH_LDFLAGS := $(O_DIR)/../lib/libevl.so.$(EVL_IVERSION) -lpthread -lrt
override LDFLAGS := $(BENCH_LDFLAGS) $(LDFLAGS)

$(O_DIR)/latmus: override CFLAGS := -I zephyr/latmon/src $(CFLAGS)

all: output-Makefile $(TARGETS)

install: all
	$(call inst-cmd,benchmarks,$(Q)for bin in $(BINARIES); do	\
		$(INSTALL) -D $(O_DIR)/$$bin -t $(DESTDIR)/$(bindir);	\
	done)

clean clobber mrproper: output-Makefile
	$(Q)$(RM) -f $(TARGETS) $(DEPFILES)

$(O_DIR)/%: %.c
	$(call ccld-cmd,$@,$(Q)$(CC) -o $(@) $< $(CFLAGS) $(LDFLAGS))

-include $(DEPFILES)
