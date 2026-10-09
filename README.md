# xtiger

xtiger is Jonas Minnberg's TI-92 emulator for Linux, and the original
one: this is the codebase that TiEmu and most of the other TI-68k
emulators descend from.

Jonas Minnberg wrote it in the late 1990s as "Tiger", closed source, in
SVGAlib and Xlib versions. After he had stopped working on it I (Misha
Nasledov) asked whether he would consider open sourcing it; he gave me
the source and permission to release it under the GPL, which happened in
2001. It has been forked and ported many times since — most visibly by
TiEmu, which took the emulation core and grew a full TI-89/92/92+/V200
emulator with a graphical debugger around it.

**This repository is preserved, not actively maintained.** If you want a
TI-68k emulator to actually use, use [TiEmu][]. What's here is the
original source, brought up to date in 2026 to the point where it still
builds and runs on a current system — SDL2 in place of Xlib, and a fix-up
pass for modern compilers — and then left there. Pull requests are
welcome but not watched closely; open an issue first if you want a reply.

[TiEmu]: https://sourceforge.net/projects/gtktiemu/

There are two frontends:

- **tiger** uses SDL2, so it runs natively on Wayland and X11 (chosen automatically at runtime), and should be portable to other operating systems SDL2 supports.
- **tiger-term** renders the LCD directly in your terminal with 24-bit color, picking the sharpest mode your terminal size allows (Ctrl-T cycles): half-blocks (240x64 cells, per-pixel gray), quadrants (120x64), sextants (120x43, needs Unicode 13 glyphs), or braille (120x32, fits anywhere).

Earlier versions used SVGAlib, Xlib, and later libggi.

## Building

Requires a C compiler, GNU make, and SDL2 development headers
(`libsdl2-dev` on Debian/Ubuntu, `SDL2-devel` on Fedora).

    make
    make install [PREFIX=...]

The 68k CPU core is generated at build time by `build68k`/`gencpu`,
a process inherited from UAE.

## Running

You need a TI-92 ROM image (1MB or 2MB), which xtiger looks for as
`ti92.rom` in the current directory:

    ./tiger [-r romfile] [-c cfgfile]

Machine state is saved to `ti92.mem` on exit and restored on startup.

## Keys

Most keys map directly (letters, digits, F1-F8, arrows, Enter,
Escape, Backspace, and the usual arithmetic keys). Notable mappings:

    Shift        SHIFT           Ctrl         DIAMOND
    Alt          2ND             Caps Lock    HAND
    Tab          STO>            Home         CLEAR
    PageUp       MODE            Scroll Lock  ON
    Insert       LN              Delete       SIN
    End          COS             PageDown     TAN
    F12          APPS            ( or [       (
    ) or ]       )               ;            THETA
    <            (-) negate

    F9   enter the built-in debugger
    F10  enter command mode (load files, change settings)
    F11  quit

### tiger-term specifics

Terminals have no key-release events, so a key reads as held for a
short time after each press (keyboard autorepeat keeps it held), and
modifiers work differently:

    UPPERCASE letter   SHIFT + letter
    Alt + key          2ND + key
    Ctrl + key         DIAMOND + key
    Ctrl-O             ON
    Ctrl-T             cycle render mode (braille/sextant/quadrant/half-block)
    Ctrl-L             force redraw
    Ctrl-Q             quit

## Command mode

Press F10 to enter command mode on the terminal. `help` lists
commands; `load file.92p` sends a file over the emulated link port;
`quit` returns to the emulator. Settings can also be placed in a
config file (default `tiger.cfg`; see the `putcfg` command).

## Credits and license

Originally written by Jonas Minnberg; released as open source and
maintained since 2001 by Misha Nasledov. The 68k emulation core, and the
build-time code generation that produces it, come from UAE. See `AUTHORS`
and `ChangeLog` for the details, and `HACKING` for Jonas's original notes
on the source.

Licensed under the GNU General Public License; see `COPYING`.

No ROM images are included or distributed here. TI-92 ROMs are
copyrighted by Texas Instruments and must be dumped from a calculator you
own.
