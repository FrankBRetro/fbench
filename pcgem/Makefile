# Fbench for PC GEM (FreeGEM / OpenGEM on DOS), 32-bit with DJGPP
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
#
#   source ~/work/vibe/PC-GEM/scripts/djgppenv.sh
#   make -C pcgem                 -> pcgem/FBENCH.APP
#
# Fbench's sources (..) are compiled with -DPCGEM against GEMLib-style
# headers in pcgem/include (gem.h, vdi.h, osbind.h, gemx.h, mintbind.h ...),
# implemented here on John Elliott's DJGPP GEM bindings: pgaes.c and pgvdi.c
# (trees, strings and bitmaps copied to DOS memory for GEM), pgrsc.c and
# pgtos.c (the TOS calls, on DOS).
#
# Run it with CWSDPMI.EXE beside it or on the PATH; the GDOS tests want
# XGDOS (XGDOS GEMVDI in GEM.BAT).

DJCC   ?= i586-pc-msdosdjgpp-gcc
GEMKIT ?= $(HOME)/work/vibe/PC-GEM/tools/djgppgem
GK      = $(GEMKIT)/CONTRIB/GEM
GKINC   = obj/kitinc

CFLAGS   = -O2 -Wall -std=gnu89 -Wno-address-of-packed-member -DPCGEM \
           -Iinclude -I..
KITFLAGS = -O2 -Wall -Wno-address-of-packed-member -fgnu89-inline -I$(GKINC) -I.
ifdef DEBUG
CFLAGS  += -DFB_DEBUG=1
endif

SRCS    = fbench tests sysinfo report pdf pdfrep cpu refbase
LAYER   = pgrsc pgtos pglinear          # the PC GEM layer, GEMLib-style headers
KITPORT = pgaes pgvdi                   # the PC GEM layer, the kit's headers

OBJS = $(addprefix obj/,$(addsuffix .o,$(SRCS) $(LAYER) $(KITPORT) pclog))

FBENCH.APP: $(OBJS) ../version.c ../VERSION ../BUILD
	@b=$$(cat ../BUILD 2>/dev/null || echo 0); \
	 echo "Fbench $$(cat ../VERSION).$$b for PC GEM"; \
	 $(DJCC) $(CFLAGS) -DFB_VER='"'$$(cat ../VERSION)'"' -DFB_BUILD=$$b -c ../version.c -o obj/version.o
	$(DJCC) -o fbench.exe $(OBJS) obj/version.o $(GK)/LIB/LIBGEM.A
	cp fbench.exe $@

obj/%.o: ../%.c ../fbench.h ../pdf.h $(wildcard include/*.h include/mint/*.h)
	@mkdir -p obj
	$(DJCC) $(CFLAGS) -c -o $@ $<
obj/pgrsc.o obj/pgtos.o obj/pglinear.o: obj/%.o: %.c
	@mkdir -p obj
	$(DJCC) $(CFLAGS) -c -o $@ $<
obj/pgaes.o obj/pgvdi.o: obj/%.o: %.c pgkit.h $(GKINC)/.fixed
	@mkdir -p obj
	$(DJCC) $(KITFLAGS) -c -o $@ $<
obj/pclog.o: pclog.c
	@mkdir -p obj
	$(DJCC) $(CFLAGS) -c -o $@ $<

# the kit's headers with their struct packing fixed for current gcc
$(GKINC)/.fixed: $(wildcard $(GK)/INCLUDE/*.H)
	@test -f $(GK)/INCLUDE/DJGPPGEM.H || { echo "DJGPP GEM kit not found: set GEMKIT"; exit 1; }
	rm -rf $(GKINC); mkdir -p $(GKINC)
	for f in $(GK)/INCLUDE/*.H; do \
	  lc=$$(basename "$$f" | tr A-Z a-z); \
	  sed -E 's/^\}[[:space:]]*([A-Za-z_0-9]+)[[:space:]]*(__attribute__\(\(packed\)\))?[[:space:]]*GPK/} __attribute__((packed)) \1;/' "$$f" > $(GKINC)/$$lc; \
	done
	touch $@

# into DOSBox-X's C: (C:\FBENCH), with CWSDPMI
PCGEMC ?= $(HOME)/work/vibe/PC-GEM/dos/C
install: FBENCH.APP
	mkdir -p $(PCGEMC)/FBENCH
	cp FBENCH.APP ../baseline/*.FBP $(PCGEMC)/FBENCH/
	test -f $(PCGEMC)/FBENCH/CWSDPMI.EXE || cp $(HOME)/work/vibe/PC-GEM/tools/cwsdpmi/bin/CWSDPMI.EXE $(PCGEMC)/FBENCH/

clean:
	rm -rf obj fbench.exe FBENCH.APP

.PHONY: clean install
