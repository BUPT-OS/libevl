# SPDX-License-Identifier: MIT

include ../config.mk

O_UAPI = $(O_DIR)/uapi

TARGETS := uapi

uapi: $(O_UAPI)

$(O_UAPI): $(O_DIR)/.uapi_stamp
	$(Q)$(MKDIR_P) $(O_UAPI)
	$(Q)$(RM) -f $(O_UAPI)/asm $(O_UAPI)/evl $(O_UAPI)/linux
	$(Q)if test -r $(UAPI)/Kbuild; then					\
		$(LN_S) $(UAPI)/arch/$(ARCH)/include/uapi/asm $(O_UAPI)/asm;	\
		$(LN_S) $(UAPI)/include/uapi/evl $(O_UAPI)/evl;			\
		$(LN_S) $(UAPI)/include/uapi/linux $(O_UAPI)/linux;		\
	elif test \! -L	$(O_UAPI)/asm; then					\
		$(LN_S) $(UAPI)/asm $(O_UAPI)/asm;				\
		$(LN_S) $(UAPI)/evl $(O_UAPI)/evl;				\
		$(LN_S) $(UAPI)/linux $(O_UAPI)/linux;				\
	fi

$(O_DIR)/.uapi_stamp: FORCE
	@touch $@

all: output-Makefile $(TARGETS)

# Remove the UAPI links. Careful with removing $(O_UAPI), just in case.
clean clobber mrproper: output-Makefile
	$(Q)$(RM) -f $(O_DIR)/.uapi_stamp $(O_UAPI)/asm $(O_UAPI)/evl $(O_UAPI)/linux
	$(Q)test -d $(O_UAPI) && $(RMDIR_SAFE) $(O_UAPI) || :

install: all
	$(Q)$(MKDIR_P) $(DESTDIR)/$(includedir)/uapi
	$(call inst-cmd,uapi-headers,cd $(O_UAPI) && find -L evl linux \! \( -name '*~' \) -type f | $(CPIO) -Lpdum --quiet $(DESTDIR)/$(includedir)/uapi)
	$(call inst-cmd,interface-headers,find evl \! \( -name '*~' \) -type f | $(CPIO) -Lpdum --quiet $(DESTDIR)/$(includedir))
	$(call inst-cmd,eshi-headers,find eshi \! \( -name '*~' \) -type f | $(CPIO) -Lpdum --quiet $(DESTDIR)/$(includedir))
	$(call inst-cmd,eshi-headers,$(CP) evl/atomic.h evl/list.h evl/heap.h $(DESTDIR)/$(includedir)/eshi/evl)
