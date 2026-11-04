# Fbench - GEM VDI / AES / GDOS benchmark
# Cross build with the MiNT toolchain (m68k-atari-mint-gcc + gemlib + mintlib)
#
# Version = VERSION file (release) + BUILD counter, which goes up by one
# every time fbench.prg is linked: 1.1.1, 1.1.2, ...

CROSS   = m68k-atari-mint-
CC      = $(CROSS)gcc
STRIP   = $(CROSS)strip
CFLAGS  = -m68000 -O2 -fomit-frame-pointer -Wall
LDLIBS  = -lgem

OBJS    = fbench.o tests.o sysinfo.o report.o pdf.o pdfrep.o cpu.o

RELEASE = release/FBENCH.PRG

all: $(RELEASE)

# the binary goes straight into release/ (committed with the sources)
$(RELEASE): $(OBJS) version.c VERSION
	@b=$$(cat BUILD 2>/dev/null || echo 0); b=$$((b + 1)); echo $$b > BUILD; \
	 echo "Fbench $$(cat VERSION).$$b"; \
	 $(CC) $(CFLAGS) -DFB_VER='"'$$(cat VERSION)'"' -DFB_BUILD=$$b -c version.c -o version.o
	@mkdir -p release
	$(CC) $(CFLAGS) -o $@ $(OBJS) version.o $(LDLIBS)
	$(STRIP) $@

%.o: %.c fbench.h pdf.h
	$(CC) $(CFLAGS) -c $<

# leaves release/ alone: released binaries are kept
clean:
	rm -f $(OBJS) version.o fbench.prg

.PHONY: all clean
