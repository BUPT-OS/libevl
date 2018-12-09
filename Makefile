include scripts/config.mk

GOALS := all clean clobber mrproper install
TARGETS := include lib tests utils commands

$(GOALS):
	@for target in $(TARGETS); do					\
		$(MAKE) -C $$target O=$(O_DIR)/$$target V=$(V) $@;	\
	done

$(TARGETS):
	$(Q)$(MAKE) -C $@ O=$(O_DIR)/$@ V=$(V)

lib: include

tests utils: lib

.PHONY: $(GOALS) $(TARGETS)
