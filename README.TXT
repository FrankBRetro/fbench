FBENCH 1.1 - GEM VDI / AES / GDOS benchmark
=============================================

Runs on ST / STE / Mega STE / TT / Falcon, single TOS or MultiTOS/MiNT,
in every ST resolution (and TT/Falcon modes). Uses more colour for the
results when the screen has 4 or 16+ colours; never changes the palette.

BUILD
  make            (needs m68k-atari-mint-gcc, gemlib, mintlib)
  -> release/FBENCH.PRG   (no resource file needed - menu is built at run time)
  Version: VERSION holds the release (1.1); BUILD is a counter that make
  raises by one every time FBENCH.PRG is linked, giving 1.1.1, 1.1.2...
  The full number is in About, every report and every profile.

MENUS  (short titles are used in ST Low)
  Fbench    About; desk accessories. Fbench also registers its name in
            the desk menu under MultiTOS (menu_register).
  File      Save profile... (^S)  - writes a *.FBP baseline of this machine
            Export report... (^E) - human readable *.TXT report
            Results report (PDF)... - this machine's results and full
              configuration; no baseline needed (if one is loaded, its
              figures and % are included too)
            Blitter report (PDF)... - runs the selected tests with the
              blitter off, then on, and writes a PDF comparison
            Compare folder (PDF)... - open a folder in the file
              selector and press OK; every *.FBP in it is compared and
              COMPARE.PDF is written into that folder
            Quit (^Q)
  Tests     Run all (^A), Run selected (^R), Select all / none / VDI /
            AES / alignment / full-screen / GDOS
  Baseline  Load baseline... (^O) - up to 4 profiles from other machines;
            pick the active one (shown as % and bars), Clear baselines
  Options   Machine description... - free text naming this setup, e.g.
              "FireBee", "MultiTOS", "XGEM"; saved in FBENCH.INF
            Blitter enabled (^B) - switches blitter use (see BLITTER below),
            test duration 0.5 / 1 / 3 s, Next GDOS font (^F),
            Mouse visible in tests - see below
  Window    Results, System info (^I)

RESULTS WINDOW
  Click a check box to include/exclude a test, click a group bar to
  toggle the whole group. Values are operations per second. With a
  baseline loaded: % of baseline and a green (faster) / red (slower)
  bar with a 100% mark. Without one: bars relative to the group's best.
  The results window closes while tests run and reopens in the same
  place afterwards. Esc or closing the test window stops a run.

TEST GROUPS
  VDI primitives        lines, fills, shapes, text, blits, raster ops
  AES windows & events  window create/open/move/size/top, sliders,
                        scrolling, objc_draw, menu bar, events, messages
  Alignment (blitter)   each test twice: on the 16 pixel (word) grid, and
                        deliberately off it (x+3 / +5 / +11, 3 px scrolls,
                        windows whose work area starts off-grid). Run the
                        group with the blitter on, save a profile, run it
                        with the blitter off and load the first profile
                        as a baseline to see the difference.
                        The note column reports the window alignment the
                        AES actually gave ("work x mod 16 = n").
  Full-screen windows   a window covering the whole desktop: open/close,
                        full<->normal, topping, fill, scroll up a line,
                        scroll left 16 / 8 / 3 px, scroll text at 8-48pt
                        (one test per size), and scroll with a different
                        size on every line
  GDOS fonts            text at 6-72pt, effects, vst_point, font switch,
                        arbitrary sizes, mixed-size scrolling, extents,
                        name enumeration, font load/unload

BLITTER
  Fbench asks whoever really decides: under XGEM it uses XGEM's own
  control (AES call XG_BLITTER) - XGEM's VDI picks blitter use itself,
  also on the Falcon, and ignores XBIOS 64. Otherwise XBIOS 64 is used,
  and the result is checked by switching and reading back (TOS 4.x
  accepts the call but ignores it). System info shows which control is
  in use; profiles record BLITSRC=xgem/xbios and BLITCTL.

COLOURS AND THEMES
  Fbench never changes the palette. Pen 0 is the window background and
  pen 1 the text, as GEM (and every XGEM theme) expects; its green, red,
  blue, cyan and highlight colours are the nearest pens actually in the
  palette (read with vq_color), used only if close enough to the wanted
  colour and visible on pen 0 - otherwise patterns in the text pen.
  Pens are re-read when a run starts and when XGEM reports a theme
  change (XM_THEME); System info lists the pens chosen.

CPU
  The CPU is probed directly (which control registers exist: VBR, CACR,
  ITT0, PCR, and the 030's data-cache bit), not taken from the _CPU
  cookie, which the ROM writes and gets wrong on accelerated machines
  (e.g. a 68040 board reported as 68020). If the cookie disagrees,
  System info says so. Profiles record CPU=.

PROFILES (*.FBP)
  Plain text, KEY=value (ops/s). Run Fbench on each machine, save a
  profile, copy the .FBP files around and load them as baselines.
  NAME= is the machine description (Options menu) or, if none is set,
  the automatic summary; SYSTEM= always holds the automatic summary
  (machine, TOS, screen mode, blitter). Both appear in the reports.

PROGRESS AND REDRAWS
  Every report (text export, blitter PDF, compare folder) shows a small
  movable progress window with a bar and the current step, and keeps
  other windows redrawn while it works. When a file selector or dialog
  closes, Fbench redraws the area it covered before starting any work.

PDF REPORTS
  Written by Fbench itself (no libraries, no FPU); they use the standard
  PDF fonts (Helvetica, Courier) so open in any PDF viewer.

  Blitter report: the selected tests run twice (blitter off, then on);
  the blitter is then put back as it was. The PDF has the system summary,
  average speed-up per group, the biggest gains, and every test with
  off/on ops/s, speed-up % and a bar pair. Afterwards the results window
  shows the blitter-on run against "Blitter off (this run)" as baseline.
  Takes twice as long as a normal run; Esc cancels.

  Compare folder: open the folder in the file selector and press OK (the
  file name can stay empty). Every *.FBP in it is loaded (up to 32, by
  file name) and COMPARE.PDF is written into the same folder, replacing
  any earlier one. The first one is the
  reference machine A (=100%) - name files 01xxx.FBP, 02xxx.FBP... to
  pick the order. The PDF contains:
    - the machine list (screen, planes, test window, desktop, blitter)
    - summary charts per group: raw % of A, and normalised % of A
    - raw ops/s, raw % of A, and normalised % of A for every test,
      best machine per test in bold green
  Normalised = ops/s x the work one operation does on that machine:
    planes  fixed-size drawing (blits, text, small fills)
    len     lines across the window   ((w + h) / 2 x planes)
    min     arcs / rounded boxes      (min(w, h) x planes)
    dim2    filled shapes             (min(w, h)^2 x planes)
    area    whole-window work         (w x h x planes of the test window)
    desk    full-screen windows       (desktop w x h x planes)
    calls   AES / event / query calls (not adjusted)
  Profiles now record PLANES, COLOURS, DESK and TESTWIN for this. Older
  profiles still load; their sizes are estimated (marked * in the PDF).

MOUSE VISIBLE IN TESTS  (Options menu; on by default under MultiTOS)
  The pointer is never hidden during a run, so you can keep using other
  programs. It shows the busy bee only while it is over Fbench's menu bar
  or windows, and the normal arrow elsewhere. The pointer shape is
  updated between tests (not inside the timed loop, so scores are not
  affected) - expect up to one test's duration of lag. Moving the
  pointer over the test window may leave small trails in the drawing;
  that window's content is throw-away and the trails don't change the
  timings much. Turn the option off for the classic hidden pointer.

NOTES
  * Crash breadcrumb: before each test Fbench writes FBLAST.LOG (in the
    directory it was started from) naming that test; it is deleted when
    a run ends normally. If Fbench crashes, FBLAST.LOG says which test.
  * "Load+unload fonts" runs only 3 cycles and is off by default: some
    GDOS versions crash when fonts are loaded over and over.
  * "Text 10pt bold" (GDOS) is off by default. The VDI's text-effects
    scratch buffer is sized for the system font; GDOS fonts with skew/
    outline overrun it and corrupt memory, so this test uses bold only.
  * If the VDI workstation can't be opened with the GDOS device id
    (Getrez()+2), Fbench retries with device 1. If both fail the alert
    shows the physical handle, Getrez() and GDOS state.
  * Timing uses the 200 Hz system timer, read via Supexec.
  * Under MultiTOS other programs keep running; close them for
    repeatable numbers. Fbench holds wind_update while each test runs.
  * The blitter switch is system-wide and is restored on quit.
    NVDI and other accelerated VDIs may ignore it.
  * At start-up Fbench checks whether XBIOS 64 can really switch the
    blitter (TOS 4.x accepts the call but ignores it). If not, the
    blitter is shown as "on (fixed)", the Blitter menu item and the
    blitter report are disabled, and profiles record BLITCTL=fixed,
    which the comparison PDF marks with a dagger.
  * Options > Trace test steps (debug, off by default): each step of a
    test's first 16 operations is written to FBLAST.LOG and shown at the
    bottom of the test window, so a hang names the exact call. It writes
    to disk inside the timed loop, so leave it off for real results.
  * GDOS tests use the system font if no GDOS is loaded (noted in the
    report as "system font"); vector-only tests show n/a.

LICENCE
  Fbench is released under the MIT licence - see LICENSE.
  Copyright (c) 2026 Francis C Bullen.
  The binary is linked with gemlib (public domain) and MiNTLib (mostly
  public domain / BSD; some parts LGPL - the full Fbench source is here,
  so the binary can be rebuilt and relinked).
