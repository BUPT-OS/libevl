# SPDX-License-Identifier: MIT

include ../config.mk

O_UAPI = $(O_DIR)/uapi

TARGETS := uapi

uapi: $(O_DIR)/.uapi_stamp $(O_DIR)/uapi

$(O_DIR)/.uapi_stamp $(O_DIR)/uapi:
	$(Q)$(MKDIR_P) $(O_UAPI)
	$(Q)$(RM) -f $(O_UAPI)/asm $(O_UAPI)/evenless
	$(Q)if test -r $(UAPI)/Makefile; then					\
		$(LN_S) $(UAPI)/arch/$(ARCH)/include/uapi/asm $(O_UAPI)/asm;	\
		$(LN_S) $(UAPI)/include/uapi/evenless $(O_UAPI)/evenless;	\
	else									\
		$(LN_S) $(UAPI)/asm $(O_UAPI)/asm;				\
		$(LN_S) $(UAPI)/evenless $(O_UAPI)/evenless;			\
	fi;									\
	touch $@

all: uapi

clean clobber mrproper:
	$(Q)$(RM) -f $(O_DIR)/.uapi_stamp $(O_UAPI)/asm $(O_UAPI)/evenless

install: all
	$(Q)$(MKDIR_P) $(DESTDIR)/$(includedir)/uapi
	$(Q)(cd $(O_UAPI) && find -L evenless \! \( -name '*~' \) -type f | $(CPIO) -Lpdum --quiet $(DESTDIR)/$(includedir)/uapi)
	$(Q)(find evenless \! \( -name '*~' \) -type f | $(CPIO) -Lpdum --quiet $(DESTDIR)/$(includedir))
