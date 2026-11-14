# Fbench - GEM VDI / AES / GDOS benchmark
# Cross build with the MiNT toolchain (m68k-atari-mint-gcc + gemlib + mintlib)
#
#   make          release build -> release/FBENCH.PRG (no logging)
#   make debug    debug build   -> FBENCHD.PRG, with the FBLAST.LOG crash
#                 breadcrumbs and Options > Trace test steps
#   make dist     zips for a GitHub release, in dist/:
#                 fbench-<version>.zip        FBENCH.PRG + README.TXT + LICENSE
#                 fbench-<version>-debug.zip  FBENCHD.PRG + README.TXT + LICENSE
#
# Version = VERSION file + BUILD counter: 1.1.<BUILD>.  BUILD goes up by one
# with every git commit: the pre-commit hook (tools/hooks/pre-commit) runs
# "make bump" and "make", and adds BUILD and release/FBENCH.PRG to the
# commit, so each commit's binary carries that commit's version.  Plain
# "make" uses the current number; a debug build adds "-debug".
# Once per checkout:  git config core.hooksPath tools/hooks

CROSS   = m68k-atari-mint-
CC      = $(CROSS)gcc
STRIP   = $(CROSS)strip
CFLAGS  = -m68000 -O2 -fomit-frame-pointer -Wall
LDLIBS  = -lgem

SRCS    = fbench.c tests.c sysinfo.c report.c pdf.c pdfrep.c cpu.c refbase.c
OBJS    = $(SRCS:%.c=obj/%.o)
DOBJS   = $(SRCS:%.c=obj-debug/%.o)

RELEASE = release/FBENCH.PRG
DEBUG   = FBENCHD.PRG

all: $(RELEASE)
debug: $(DEBUG)

# next version number (the pre-commit hook calls this)
bump:
	@b=$$(cat BUILD 2>/dev/null || echo 0); echo $$((b + 1)) > BUILD; \
	 echo "Fbench $$(cat VERSION).$$(cat BUILD)"

# the release binary goes straight into release/ (committed with the sources)
$(RELEASE): $(OBJS) version.c VERSION BUILD
	@b=$$(cat BUILD 2>/dev/null || echo 0); \
	 echo "Fbench $$(cat VERSION).$$b"; \
	 $(CC) $(CFLAGS) -DFB_VER='"'$$(cat VERSION)'"' -DFB_BUILD=$$b -c version.c -o obj/version.o
	@mkdir -p release
	$(CC) $(CFLAGS) -o $@ $(OBJS) obj/version.o $(LDLIBS)
	$(STRIP) $@

$(DEBUG): $(DOBJS) version.c VERSION BUILD
	@b=$$(cat BUILD 2>/dev/null || echo 0); \
	 echo "Fbench $$(cat VERSION).$$b-debug"; \
	 $(CC) $(CFLAGS) -DFB_DEBUG=1 -DFB_VER='"'$$(cat VERSION)'"' -DFB_BUILD=$$b -c version.c -o obj-debug/version.o
	$(CC) $(CFLAGS) -o $@ $(DOBJS) obj-debug/version.o $(LDLIBS)
	$(STRIP) $@

# the built-in baseline (A): regenerated when the profile changes
refbase.c: baseline/STE16BLT.FBP tools/mkref.sh
	sh tools/mkref.sh $< > $@

obj/%.o: %.c fbench.h pdf.h
	@mkdir -p obj
	$(CC) $(CFLAGS) -c $< -o $@

obj-debug/%.o: %.c fbench.h pdf.h
	@mkdir -p obj-debug
	$(CC) $(CFLAGS) -DFB_DEBUG=1 -c $< -o $@

# release zips (run after the commit, so the version matches the binary);
# the PC GEM zip too when release/FBENCH.APP is there
CWSDPMI ?= $(HOME)/work/vibe/PC-GEM/tools/cwsdpmi/bin
VER = $(shell cat VERSION).$(shell cat BUILD)
dist: $(RELEASE) $(DEBUG)
	@mkdir -p dist
	rm -f dist/fbench-$(VER).zip dist/fbench-$(VER)-debug.zip
	zip -j -X dist/fbench-$(VER).zip $(RELEASE) README.TXT LICENSE
	zip -j -X dist/fbench-$(VER)-debug.zip $(DEBUG) README.TXT LICENSE
	@echo "dist/fbench-$(VER).zip and dist/fbench-$(VER)-debug.zip ready"
	@if [ -f release/FBENCH.APP ]; then \
	  test -f $(CWSDPMI)/CWSDPMI.EXE || { echo "CWSDPMI.EXE not found: set CWSDPMI"; exit 1; }; \
	  rm -rf dist/pcgem dist/fbench-$(VER)-pcgem.zip; mkdir -p dist/pcgem/FBENCH; \
	  cp release/FBENCH.APP README.TXT dist/pcgem/FBENCH/; \
	  cp LICENSE dist/pcgem/FBENCH/LICENSE.TXT; \
	  cp $(CWSDPMI)/CWSDPMI.EXE dist/pcgem/FBENCH/; \
	  cp $(CWSDPMI)/cwsdpmi.doc dist/pcgem/FBENCH/CWSDPMI.DOC; \
	  (cd dist/pcgem && zip -r -X ../fbench-$(VER)-pcgem.zip FBENCH); \
	  rm -rf dist/pcgem; \
	  echo "dist/fbench-$(VER)-pcgem.zip ready (PC GEM)"; \
	fi

# leaves release/ alone: released binaries are kept
clean:
	rm -rf obj obj-debug $(DEBUG) *.o fbench.prg

.PHONY: all debug bump dist clean
