# SPDX-License-Identifier: MIT

include config.mk

TARGETS := include lib tests utils commands

$(MAIN_GOALS):
	@for target in $(TARGETS); do					\
		$(MAKE) -C $$target O=$(O_DIR)/$$target V=$(V) $@;	\
	done

lib: include

tests utils: lib
