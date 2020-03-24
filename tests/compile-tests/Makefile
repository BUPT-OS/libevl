# SPDX-License-Identifier: MIT

include ../../config.mk
include ../../libversion.mk

C_SRC := $(wildcard *.c)
CPLUS_SRC := $(wildcard *.cc)
TARGETS = $(C_SRC:%.c=$(O_DIR)/%) $(CPLUS_SRC:%.cc=$(O_DIR)/%)
DEPFILES = $(C_SRC:%.c=$(O_DIR)/%.d) $(CPLUS_SRC:%.cc=$(O_DIR)/%.d)

TEST_CPPFLAGS := $(BASE_CPPFLAGS)	\
		 -I.			\
		 -I../../include	\
		 -I$(O_DIR)/../../include
TEST_CFLAGS := $(TEST_CPPFLAGS) $(BASE_CFLAGS)
override CFLAGS := $(TEST_CFLAGS) $(CFLAGS)

TEST_CXXFLAGS := $(TEST_CPPFLAGS) $(BASE_CXXFLAGS)
override CXXFLAGS := $(TEST_CXXFLAGS) $(CXXFLAGS)

TEST_LDFLAGS := $(O_DIR)/../../lib/libevl.so.$(EVL_IVERSION) -lpthread -lrt $(LDFLAGS)

all: output-Makefile $(DEPFILES) $(TARGETS)

clean clobber mrproper:
	$(Q)$(RM) -f $(TARGETS) $(DEPFILES)

install:

$(O_DIR)/%: %.c $(O_DIR)/%.d
	$(call ccld-cmd,$@,$(Q)$(CC) -o $(@) $< $(TEST_CFLAGS) $(TEST_LDFLAGS))

$(O_DIR)/%: %.cc $(O_DIR)/%.d
	$(call cxxld-cmd,$@,$(Q)$(CXX) -o $(@) $< $(TEST_CXXFLAGS) $(TEST_LDFLAGS))

-include $(DEPFILES)
