# SPDX-License-Identifier: MIT

EVL_SERIAL	:= 0
EVL_IVERSION	:= 0

$(O_DIR)/git-stamp.h: git-stamp
	@if test -r ../.git; then							\
	  stamp=`git --git-dir=../.git rev-list --abbrev-commit -1 HEAD`;		\
	  if test \! -s $@ || grep -wvq $$stamp $@; then				\
		date=`git --git-dir=../.git log -1 $$stamp --pretty=format:%ci`;	\
		echo "#define GIT_STAMP \"#$$stamp ($$date)\"" > $@;			\
	  fi;										\
	elif test \! -r $@ -o -s $@; then						\
	    $(RM) -f $@ && touch $@;							\
	fi; true

.PHONY: git-stamp
