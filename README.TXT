FBENCH 1.1 - GEM VDI / AES / GDOS benchmark
=============================================

Runs on ST / STE / Mega STE / TT / Falcon, single TOS or MultiTOS/MiNT,
in every ST resolution (and TT/Falcon modes). Uses more colour for the
results when the screen has 4 or 16+ colours; never changes the palette.

BUILD
  make            release build -> release/FBENCH.PRG (no logging)
  make debug      debug build   -> FBENCHD.PRG: writes the FBLAST.LOG crash
                  breadcrumbs and has Options > Trace test steps
  (needs m68k-atari-mint-gcc, gemlib, mintlib; no resource file needed -
  the menu is built at run time)
  make -C pcgem   PC GEM build -> pcgem/FBENCH.APP (see PC GEM below)

  Version: 1.1.<BUILD>, from the VERSION and BUILD files. Every git commit
  gets the next number: the pre-commit hook in tools/hooks bumps BUILD,
  rebuilds release/FBENCH.PRG and adds both to the commit, so a commit's
  binary always shows that commit's version. Enable it once per checkout:
      git config core.hooksPath tools/hooks
  Plain "make" does not change the number. The version is shown in About,
  the results window title, every report and every profile.

MENUS  (short titles are used in ST Low)
  Fbench    About; desk accessories. Fbench also registers its name in
            the desk menu under MultiTOS (menu_register).
  File      Save profile... (^S)  - writes a *.FBP baseline of this machine
            Export report... (^E) - human readable *.TXT report
            Results report (PDF)... - this machine's results and full
              configuration; no baseline needed (if one is loaded, its
              figures, raw % and normalised % are included too)
            Blitter report (PDF)... - runs the selected tests with the
              blitter off, then on, and writes a PDF comparison
            Compare folder (PDF)... - open a folder in the file
              selector and press OK; every *.FBP in it is compared and
              COMPARE.PDF is written into that folder
            Quit (^Q)
  Tests     Run all (^A), Run selected (^R), Select all / none / VDI /
            AES / alignment / full-screen / GDOS
  Baseline  A: the built-in reference (see BUILT-IN BASELINE)
            Load baseline... (^O) - up to 3 more profiles from other
            machines; pick the active one (shown as % and bars)
            Clear baselines - removes the loaded ones, keeps A
  Options   Machine description... - free text naming this setup, e.g.
              "FireBee", "MultiTOS", "XGEM"; saved in FBENCH.INF
            Blitter enabled (^B) - switches blitter use (see BLITTER below),
            test duration 0.5 / 1 / 3 s, Long run (10 runs) - see
            LONG RUN below, Next GDOS font (^F),
            Mouse visible in tests - see below
            Stay in background - see below
  Window    Results, System info (^I)

RESULTS WINDOW
  Click a check box to include/exclude a test, click a group bar to
  toggle the whole group. Values are operations per second. With a
  baseline loaded: % of baseline (shown as e.g. "280x" from 10000% up)
  and a two-colour bar: the baseline's share on the left (patterned),
  this machine's on the right (green = faster, red = slower), with a
  line where they meet and a short mark in the middle (equal speed).
  Either share keeps at least one pixel, so a much slower machine still
  shows. Without a baseline: bars relative to the group's best.
  The results window closes while tests run and reopens in the same
  place afterwards. Esc or closing the test window stops a run.

TEST GROUPS
  VDI primitives        lines, fills, shapes, text, blits, raster ops
  AES windows & events  window create/open/move/size/top, sliders,
                        scrolling (plain, and in a window with arrows and
                        a slider that follows the scroll), objc_draw,
                        menu bar, events, messages;
                        whole dialogs drawn with objc_draw: medium (~18
                        objects), complex (~42), very complex (~76,
                        nested 4 deep: lists, slider, toolbar, image,
                        tabs, swatches, every object state) and
                        pathological (~140: three full-size layers drawn
                        over each other, boxes nested 6 deep (the most
                        TOS objc_draw can walk safely), every state
                        on one object, 3 px borders, images off the grid,
                        overlaps, transparent fills). The note
                        gives the object count; in a small test window
                        the rows are packed closer, and anything that
                        still doesn't fit is left out
  Alignment (blitter)   each test twice: on the 16 pixel (word) grid, and
                        deliberately off it (x+3 / +5 / +11, 3 px scrolls,
                        windows whose work area starts off-grid). Run the
                        group with the blitter on, save a profile, run it
                        with the blitter off and load the first profile
                        as a baseline to see the difference.
                        The note column reports the window alignment the
                        AES actually gave ("work x mod 16 = n").
                        The dialogs are drawn here too, on the 16 pixel
                        grid and at grid + 3.
                        Window scrolling right, left, diagonally down-right
                        and up-left, each in a window without scroll bars
                        and in one with both sliders (moved every step),
                        with the work area on the grid scrolling 16 px, and
                        off it (grid + 5) scrolling 3 px. "Right" means the
                        view moves right, as with the window's right arrow.
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

SAME STARTING POSITIONS ON EVERY MACHINE
  The test window's work area starts on a 16 pixel boundary, and so do
  the windows the AES and scrolling tests open (unless a test puts one
  off the grid on purpose), so scrolls and blits start at the same bit
  positions on every machine. Shapes and text still use the whole test
  window. The random positions and sizes come from a sequence seeded from
  each test's name, so they don't depend on which tests ran before, and
  every run of a long run draws the same things.

LONG RUN
  Options > Long run (10 runs): Run all / Run selected repeats the whole
  selection 10 times (an alert shows the minimum time first; Esc stops
  and keeps the runs done so far). Each test then shows the average,
  and a line across its bar from the slowest to the fastest run. The
  Results report and Export report give each test's swing (how far the
  slowest and fastest runs were from the average, e.g. "-1.2% / +0.8%")
  and a Consistency section: how many tests stayed within 1% and 5%, and
  the most variable ones. Save profile stores RUNS=10 and, per test, the
  average followed by the slowest and fastest run; older Fbench versions
  read just the average.
  Each run starts from the same GDOS state: the benchmark workstation is
  opened again and its fonts reloaded before every run, and "Load+unload
  fonts" (which can change the GDOS's state for the tests after it) runs
  only once, at the end of the last run.

BUILT-IN BASELINE
  Fbench carries one reference profile inside the program, made on an
  STE with TOS 1.6, the blitter on and XGDOS, in medium resolution
  (640x200, 4 colours), run in cycle-exact Hatari: "STE TOS 1.6 blitter
  XGDOS (Hatari) [built-in]". It is always baseline A and the active one
  when Fbench starts, so the results window, Export report and Results
  report show every machine against the same STE out of the box (raw %
  and normalised %). Compare folder always puts it first as machine A
  (=100%); a copy of STE16BLT.FBP in the folder is skipped.
  The source is baseline/STE16BLT.FBP; the Makefile turns it into
  refbase.c (tools/mkref.sh) when it changes.

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
  With a baseline loaded, the Results report and Export report end with
  a conclusion in plain words: each group as "280x faster" / "35% faster"
  / "about the same speed" / "2.5x slower" than the baseline, raw and
  normalised, the overall figure, and the tests furthest ahead and
  behind. Group figures are the mean of the per-test percentages, as in
  the Groups summary.

  Written by Fbench itself (no libraries, no FPU); they use the standard
  PDF fonts (Helvetica, Courier) so open in any PDF viewer.

  Blitter report: the selected tests run twice (blitter off, then on);
  the blitter is then put back as it was. The PDF has the system summary,
  average speed-up per group, the biggest gains, and every test with
  off/on ops/s, speed-up % and the same two-colour line as the Results
  report (blitter off's share in grey, on's in blue; past the middle the
  blitter helps). It ends with a conclusion in plain words per group,
  overall, the biggest gain and loss, and how many tests don't change. Afterwards the results window
  shows the blitter-on run against "Blitter off (this run)" as baseline.
  Takes twice as long as a normal run; Esc cancels.

  Compare folder: open the folder in the file selector and press OK (the
  file name can stay empty). Every *.FBP in it is loaded (up to 31, by
  file name) and COMPARE.PDF is written into the same folder, replacing
  any earlier one. Machine A (=100%) is always the built-in STE
  baseline; the folder's profiles follow as B, C... by file name - name
  files 01xxx.FBP, 02xxx.FBP... to pick the order. The PDF contains:
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
  The same normalisation is in the Results report (PDF) and the text
  Export report whenever a baseline is loaded: a Norm % column next to the
  raw %, and normalised group averages.
  Profiles now record PLANES, COLOURS, DESK and TESTWIN for this. Older
  profiles still load; their sizes are estimated (marked * in the PDF).

STAY IN BACKGROUND  (Options menu; on by default under MagiC and MultiTOS)
  Lets you keep working in other programs while Fbench runs. Under MagiC
  (Atari, MagiC Mac, MagiC PC, MagiCOnLinux) the program that owns the
  top window gets the menu bar and the keyboard, so Fbench must not top
  its windows. With this option on:
    * Fbench never brings its windows to the front by itself.
    * The run carries on when other windows cover the test window. Each
      test still draws its whole area, so the timings stay the same as
      when it is visible; anything covering that area shows Fbench's
      drawing until the test ends (0.5 - 3 s), then Fbench asks the AES
      to repaint it (form_dial FMD_FINISH) and those programs redraw.
    * Tests that have to open, top, move or resize windows, or redraw the
      menu bar (AES window tests, alignment window tests, full-screen
      tests), run only while Fbench is the program in front. Otherwise
      they are skipped and shown as n/a with "skipped: in background".
    * At the end of a run the results window reopens behind the program
      you are using (where the AES supports WF_BOTTOM).
  Turn it off for an unattended run that should always complete every
  test. Single-tasking TOS works as before (the option is off there).
  MagiC is recognised by its MagX cookie and counts as multitasking.

MOUSE VISIBLE IN TESTS  (Options menu; on by default under MultiTOS)
  The pointer is never hidden during a run, so you can keep using other
  programs. It shows the busy bee only while it is over Fbench's menu bar
  or windows, and the normal arrow elsewhere. The pointer shape is
  updated between tests (not inside the timed loop, so scores are not
  affected) - expect up to one test's duration of lag. Moving the
  pointer over the test window may leave small trails in the drawing;
  that window's content is throw-away and the trails don't change the
  timings much. Turn the option off for the classic hidden pointer.

PC GEM (FreeGEM / OpenGEM on DOS)
  release/FBENCH.APP is Fbench for PC GEM: the same program, built as a
  32-bit DJGPP program with -DPCGEM (pcgem/Makefile). It runs every test;
  the timer is DJGPP's uclock() (1.19 MHz) counted in the same 200 Hz
  ticks, so results compare with the Atari's. System info shows the x86
  CPU family, DOS, the DPMI host and GEM's AES version instead of TOS and
  the cookie jar; a PC has no blitter. It needs a 386 or later, a DPMI host
  (CWSDPMI.EXE beside it or on the PATH) and, for the GDOS tests, XGDOS for
  PC GEM (https://github.com/FrankBRetro/xgdos), which gives it Speedo
  fonts. It needs OpenGEM or FreeGEM (GEM 3 or later): the original
  Digital Research GEM 1.x (AES 1.2, 1985) lacks AES calls Fbench uses,
  so on it Fbench says it isn't supported and quits.
  It uses EdWord's PC GEM layer (../edword/pcgem, or EDPC=...):
      source ~/work/vibe/PC-GEM/scripts/djgppenv.sh
      make -C pcgem          -> pcgem/FBENCH.APP
  The pre-commit hook builds it too when the DJGPP compiler is on the
  PATH, and copies it to release/. "make dist" adds
  fbench-<version>-pcgem.zip (FBENCH.APP, CWSDPMI, this file, the
  licences).

NOTES
  * Each test does its operation once, untimed, before timing starts, so
    one-off costs don't land in the 1 s measurement - e.g. XGDOS builds
    and caches a font size's glyphs the first time it is drawn (about
    1.2 s on an STE, then ~0.08 s a line). If that first operation was
    much slower than the rest, the note says so ("1st op 1.25 s").
  * Debug build only (make debug): before each test Fbench writes
    FBLAST.LOG (in the directory it was started from) naming that test;
    it is deleted when a run ends normally. If Fbench crashes, FBLAST.LOG
    says which test. The release build writes no log.
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
  * Options > Trace test steps (debug build only, off by default): each step of a
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
  The PC GEM binary (FBENCH.APP) is linked with John Elliott's GEM
  bindings for DJGPP, based on Digital Research's GEM Programmer's
  Toolkit and under the GNU GPL version 2, so that binary as a whole is
  passed on under the GPL version 2; Fbench's own source stays MIT. The
  bindings' source: https://www.seasip.info/Gem/Software/djgppgem.zip
  CWSDPMI.EXE (in the PC zip) is Copyright (C) 1995-2010 Charles W
  Sandmann, unmodified, with CWSDPMI.DOC; you have the right to its source
  code and updates: http://www.delorie.com/djgpp/ (csdpmi7s.zip).
