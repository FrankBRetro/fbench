# Fbench for PC GEM (FreeGEM / OpenGEM on DOS), 32-bit with DJGPP
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
#
#   source ~/work/vibe/PC-GEM/scripts/djgppenv.sh
#   make -C pcgem                 -> pcgem/FBENCH.APP
#
# Fbench's sources (..) are compiled with -DPCGEM against GEMLib-style
# headers. The PC GEM layer they need - gem.h / vdi.h / osbind.h and their
# implementation on John Elliott's DJGPP GEM bindings - is EdWord's
# (../../edword/pcgem, set EDPC to move it); pcgem/include here adds only
# Fbench's own gemx.h and mintbind.h.
#
# Run it with CWSDPMI.EXE beside it or on the PATH; the GDOS tests want
# XGDOS (XGDOS GEMVDI in GEM.BAT).

DJCC   ?= i586-pc-msdosdjgpp-gcc
GEMKIT ?= $(HOME)/work/vibe/PC-GEM/tools/djgppgem
EDPC   ?= ../../edword/pcgem
GK      = $(GEMKIT)/CONTRIB/GEM
GKINC   = obj/kitinc

CFLAGS   = -O2 -Wall -std=gnu89 -Wno-address-of-packed-member -DPCGEM \
           -Iinclude -I$(EDPC)/include -I..
KITFLAGS = -O2 -Wall -Wno-address-of-packed-member -fgnu89-inline -I$(GKINC) -I$(EDPC)
ifdef DEBUG
CFLAGS  += -DFB_DEBUG=1
endif

SRCS    = fbench tests sysinfo report pdf pdfrep cpu refbase
LAYER   = pgrsc pgtos pglinear          # the PC GEM layer, our headers
KITPORT = pgaes pgvdi                   # the PC GEM layer, the kit's headers

OBJS = $(addprefix obj/,$(addsuffix .o,$(SRCS) $(LAYER) $(KITPORT) pclog))

FBENCH.APP: $(OBJS) ../version.c ../VERSION ../BUILD
	@b=$$(cat ../BUILD 2>/dev/null || echo 0); \
	 echo "Fbench $$(cat ../VERSION).$$b for PC GEM"; \
	 $(DJCC) $(CFLAGS) -DFB_VER='"'$$(cat ../VERSION)'"' -DFB_BUILD=$$b -c ../version.c -o obj/version.o
	$(DJCC) -o fbench.exe $(OBJS) obj/version.o $(GK)/LIB/LIBGEM.A
	cp fbench.exe $@

obj/%.o: ../%.c ../fbench.h ../pdf.h
	@mkdir -p obj
	$(DJCC) $(CFLAGS) -c -o $@ $<
obj/pgrsc.o obj/pgtos.o obj/pglinear.o: obj/%.o: $(EDPC)/%.c
	@mkdir -p obj
	$(DJCC) $(CFLAGS) -c -o $@ $<
obj/pgaes.o obj/pgvdi.o: obj/%.o: $(EDPC)/%.c $(EDPC)/pgkit.h $(GKINC)/.fixed
	@mkdir -p obj
	$(DJCC) $(KITFLAGS) -c -o $@ $<
obj/pclog.o: pclog.c
	@mkdir -p obj
	$(DJCC) $(CFLAGS) -c -o $@ $<

# the kit's headers with their struct packing fixed for current gcc (as
# EdWord's Makefile does it)
$(GKINC)/.fixed: $(wildcard $(GK)/INCLUDE/*.H)
	@test -f $(GK)/INCLUDE/DJGPPGEM.H || { echo "DJGPP GEM kit not found: set GEMKIT"; exit 1; }
	rm -rf $(GKINC); mkdir -p $(GKINC)
	for f in $(GK)/INCLUDE/*.H; do \
	  lc=$$(basename "$$f" | tr A-Z a-z); \
	  sed -E 's/^\}[[:space:]]*([A-Za-z_0-9]+)[[:space:]]*(__attribute__\(\(packed\)\))?[[:space:]]*GPK/} __attribute__((packed)) \1;/' "$$f" > $(GKINC)/$$lc; \
	done
	touch $@

clean:
	rm -rf obj fbench.exe FBENCH.APP

.PHONY: clean
