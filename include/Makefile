# SPDX-License-Identifier: MIT

include ../config.mk

O_UAPI = $(O_DIR)/uapi

TARGETS := uapi

$(TARGETS): output-Makefile

uapi: $(O_DIR)/.uapi_stamp $(O_UAPI)

$(O_DIR)/.uapi_stamp $(O_DIR)/uapi:
	$(Q)$(MKDIR_P) $(O_UAPI)
	$(Q)$(RM) -f $(O_UAPI)/asm $(O_UAPI)/evl
	$(Q)if test -r $(UAPI)/Makefile; then					\
		$(LN_S) $(UAPI)/arch/$(ARCH)/include/uapi/asm $(O_UAPI)/asm;	\
		$(LN_S) $(UAPI)/include/uapi/evl $(O_UAPI)/evl;			\
	else									\
		$(LN_S) $(UAPI)/asm $(O_UAPI)/asm;				\
		$(LN_S) $(UAPI)/evl $(O_UAPI)/evl;				\
	fi;									\
	touch $@

all: uapi

# Remove the UAPI links. Careful with removing $(O_DIR)/uapi, just in case.
clean clobber mrproper:
	$(Q)$(RM) -f $(O_DIR)/.uapi_stamp $(O_UAPI)/asm $(O_UAPI)/evl
	$(Q)test -d $(O_UAPI) && $(RMDIR_SAFE) $(O_UAPI) || :

install: all
	$(Q)$(MKDIR_P) $(DESTDIR)/$(includedir)/uapi
	$(call inst-cmd,uapi-headers,cd $(O_UAPI) && find -L evl \! \( -name '*~' \) -type f | $(CPIO) -Lpdum --quiet $(DESTDIR)/$(includedir)/uapi)
	$(call inst-cmd,interface-headers,find evl \! \( -name '*~' \) -type f | $(CPIO) -Lpdum --quiet $(DESTDIR)/$(includedir))
