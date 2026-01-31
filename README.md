# ENGINE9000 68k Retro Debugger/Profiler

Amiga/Neo Geo debugger/profiler - under heavy development so likely to be unstable for the time being. 

Expect file format changes, regressions and other incompatibilities with new versions. 

The UI is custom SDL2, so expect some weird UI experiences.

<p align="center">
  <a href="https://www.youtube.com/watch?v=Q24F6S8J57U">
    <img
      src="assets/video.png"
      style="width: 50%; min-width: 180px; max-width: 400px;"
      alt="Demo video"
    >
  </a>
</p>

![Debugger UI](assets/debugger.png)

Project layout

- `e9k-debugger` - The debugger project
- `geo9000` - Neo Geo emulator - (forked from `geolith-libretro` https://github.com/libretro/geolith-libretro)
- `ami9000` - Amiga emulator - (fored from `libretro-uae` https://github.com/libretro/libretro-uae)

Supported platforms:

- macOS
- Windows via MinGW (`x86_64-w64-mingw32`); so far only tested by cross-compiling from macOS

---

## Overview

- ASM/C Source level debugger (needs ELF with DWARF - currently Neo Geo only)
- Amiga/Neo Geo emulators with frame level rewind/fast forward and simple CRT shader
- Source level profiler
- Trainer/cheat mode
- Smoke tester (record scenarios, replay, check all video frames identical)
- Fake Neo Geo peripherals for debug console and profile checkpoints
- Neo Geo Sprite debug visualiser

### Debugging Features

![Console](assets/console.png)

- UI or Console based debug
- Pause / continue
- Step line / step instruction / next (step over)
- Breakpoints by:
  - Absolute address
  - Symbol name
  - `file:line`
- Watchpoints, with filters such as:
  - Read/write/rw
  - Access size (`8|16|32`)
  - Address mask compare
  - Value/old-value/diff predicates
- Memory write (by address or by symbol)
- “Protect” (memory protection / cheat):
  - Block writes to an address (optionally sized)
  - Force a value at an address (optionally sized)
- Frame step
- Frame reverse
- Print - support for local vars is mixed depending on optimisations and toolchain

### Neo Geo Fake Peripherals

- `0xFFFF0` - characters written to this address will be output in the console and terminal
- `0xFFFEC` - writing a checkpoint slot from 0-64 for checkpoint profiling stats
- These overlay with ROM addresses - other emulators or real neo geo might crash if you use these

### Amiga Fake Peripherals

- `0xFC0000` - characters written to this address will be output in the console and terminal
- `0xFC0004` - writing a long word to this address sets this as the base address of the .text section
- `0xFC0008` - writing a long word to this address sets this as the base address of the .data section
- `0xFC000C` - writing a long word to this address sets this as the base address of the .bss section
- These overlay with ROM addresses - other emulators or real Amiga might crash if you use these

Checkpoints are not yet implemented on Amiga.

### Profiling Features

![Console](assets/profile.png)

There are two complementary profiling mechanisms:

- **Streaming sampler profiler**: starts/stops sampling in the emulator.
  - Aggregates samples into “Profiler Hotspots”.
  - Analysis/export can emits a web bases results view.
- **Checkpoint profiler**: a fixed set of lightweight “checkpoints”.
  - Checkpoints are set by the target by writing to a fake peripheral
  - Checkpoint execution stats displayed

### Timeline / Rewind-Oriented Tools

The debugger keeps a rolling save-state timeline (“state buffer”) implemented as keyframes + diffs:

- You can use a hidden seek bar at the bottom of the emulator window to set the current frame
- When you release the seek bar, any stats saved ahead of the seek bar are trimmed
- If you want to be able to seek around without losing data, save state first "Save" from toolbar
- Restoring this with "Restore" will restore your full timeline
- Frame-step controls (frame stepping uses the state buffer)
- `loop` between two recorded frame numbers
- `diff` memory between two recorded frame numbers

### Automation / Regression Helpers

`e9k-debugger` includes first-class capture/replay tooling:

- Input recording to a file (`--record`) and replay (`--playback`)
- Smoke test recording (`--make-smoke`) and compare mode (`--smoke-test`)
  - Designed for “record inputs + frames” and later replay/compare

### Sprite Debug Window

![Neo Geo Sprite Debug](assets/sprite_debug.png)

- Available via a hidden button in the emulator window - hover in top right hand corner to reveal
- Renders a full view of the Neo Geo coordinate space allowing visualsation of off screen sprites
- Renders a "sprite-line" histogram showing how close you are to hitting the Neo Geo sprites-per-line limits

## Trainer
- Simple training function
- Set Markers, analyse which ram addresses have changed between marker
- Protect ram locations of interest (infinite lives etc)
- Protections for matching roms are persisted, reloaded

## Save States
- e9k-debugger automaically saves a save state every frame (differential) - this allows you to rewind/fast forward etc
- When you do a "Save/Restore" it saves the entire save state buffer - this will be saved in your configured "saves" folder on exit
- Save states include a hash of the rom
- When you start if a save state is available matching the filename and hash it will be loaded ready for a restore
- This restore will include full history

## Transitions
- e9k-debugger is a playground for me to play with transitions - if you don't like them, go to the settings screen and untick "fun"

## CRT Shader
- e9k-debugger includes a relatively simple CRT shader, a settings dialog is available via a hidden button in the emulator window. Hover in the top right hand corner to reveal.

---

## Controls

Clicking on a title bar collapses the panel, for horizontal panels click the icon to restore.

Global debugger hotkeys

| Key | Action |
|---|---|
| `F1` | Help |
| `F2` | Screenshot to clipboard |
| `F3` | Toggle CRT mode |
| `F4` | Toggle FPS overlay |
| `F5` | Speed toggle |
| `F6` | Toggle audio |
| `F7` | Save state |
| `F8` | Restore state |
| `F11` | Toggle rolling record |
| `F12` | Amiga <-> Neo Geo |
| `ESC` | Toggle fullscreen |
| `TAB` | Focus the console prompt |
| `c` | Continue |
| `p` | Pause |
| `s` | Step (source line) |
| `n` | Next (step over) |
| `i` | Step instruction |
| `b` | Frame step back |
| `f` | Frame step |
| `g` | Frame continue |
| `,` | Checkpoint profiler toggle |
| `.` | Checkpoint profiler reset |
| `/` | Checkpoint profiler dump to stdout |

---

## Console Commands

### `help` (alias: `h`)

SYNOPSIS  
`help [command]`

DESCRIPTION  
Lists available commands. With an argument, prints the help/usage for that specific command (aliases are accepted).

EXAMPLES  
`help`  
`help break`  
`help b`

---

### `break` (alias: `b`)

SYNOPSIS  
`break <addr|symbol|file:line>`

DESCRIPTION  
Adds a breakpoint. Resolution order is:

1. `file:line` 
2. Hex address (24-bit; `0x` prefix optional)
3. Symbol

NOTES  
Requires a configured ELF path (Settings → `ELF`, or `--elf PATH`).

EXAMPLES  
`break 0x00A3F2`  
`break player_update`  
`break foo.c:123`

---

### `continue` (alias: `c`)

SYNOPSIS  
`continue`

DESCRIPTION  
Resumes execution defocuses the console prompt.

---

### `cls`

SYNOPSIS  
`cls`

DESCRIPTION  
Clears the console output buffer.

---

### `step` (alias: `s`)

SYNOPSIS  
`step`

DESCRIPTION  
Steps to the next source line

---

### `next` (alias: `n`)

SYNOPSIS  
`next`

DESCRIPTION  
Steps over the next line

---

### `stepi` (alias: `i`)

SYNOPSIS  
`stepi`

DESCRIPTION  
Steps a single instruction

---

### `print` (alias: `p`)

SYNOPSIS  
`print <expr>`

DESCRIPTION  
Evaluates and prints an expression using DWARF + symbol information from the configured ELF.

There is also a fast path for simple numeric expressions so that dereferences like `print *0xADDR` work even without an ELF:

- `print 1234`
- `print 0x00100000`
- `print *0x00100000`
- `print *(0x00100000)`

EXAMPLES  
`print playerLives`  
`print *0x00100000`

---

### `write`

SYNOPSIS  
`write <dest> <value>`

DESCRIPTION  
Writes a hex value to an address or symbol.

- If `<dest>` looks like a hex address (`0x...`), it writes directly to that address.
- Otherwise `<dest>` is treated as a symbol and resolved via the expression/symbol resolver.

The write size is inferred from the number of hex digits in `<value>`:

- `0xNN` → 8-bit
- `0xNNNN` → 16-bit
- `0xNNNNNNNN` → 32-bit

NOTES  
`<value>` must be strict hex with a `0x` prefix.

EXAMPLES  
`write 0x0010ABCD 0xFF`  
`write someGlobal 0x0001`

---

### `watch` (alias: `wa`)

SYNOPSIS  
`watch [addr] [r|w|rw] [size=8|16|32] [mask=0x...] [val=0x...] [old=0x...] [diff=0x...]`  
`watch del <idx>`  
`watch clear`

DESCRIPTION  
Lists, adds, or removes watchpoints.

- With no arguments, prints the current watchpoint table plus the enabled mask.
- `watch clear` resets all watchpoints.
- `watch del <idx>` removes a watchpoint by index.
- Otherwise, adds a watchpoint at `<addr>` with the selected options.

OPTIONS  
`r`, `w`, `rw` select access type. If omitted, defaults to `rw`.  
`size=8|16|32` matches access size.  
`mask=0x...` applies an address compare mask.  
`val=0x...` matches a value equality operand.  
`old=0x...` matches an old-value equality operand.  
`diff=0x...` (or `neq=0x...`) matches “value != old” (with an operand).

EXAMPLES  
`watch`  
`watch 0x0010ABCD rw size=16`  
`watch 0x0010ABCD w val=0x00000010`  
`watch del 3`  
`watch clear`

---

### `train`

SYNOPSIS  
`train <from> <to> [size=8|16|32]`  
`train ignore`  
`train clear`

DESCRIPTION  
Convenience command for “training” by breaking on a value transition.

- `train <from> <to>` installs a watchpoint that triggers when a write changes a value from `<from>` to `<to>`.
  - `<from>` and `<to>` accept decimal or `0x...` hex.
  - The watchpoint matches any address (mask `0`).
- `train ignore` adds the last triggered watchbreak address to an ignore list.
- `train clear` clears the ignore list.

EXAMPLES  
`train 3 2 size=8`  
`train 0x03 0x02`  
`train ignore`

---

### `protect`

SYNOPSIS  
`protect`  
`protect clear`  
`protect del <addr> [size=8|16|32]`  
`protect <addr> block [size=8|16|32]`  
`protect <addr> set=0x... [size=8|16|32]`

DESCRIPTION  
Manages “protect” rules:

- `block`: prevent writes to an address (optionally sized)
- `set=...`: force a value at an address (optionally sized)

With no arguments, prints the current enabled protect entries.

EXAMPLES  
`protect`  
`protect 0x0010ABCD block size=16`  
`protect 0x0010ABCD set=0x00000063 size=8`  
`protect del 0x0010ABCD size=8`  
`protect clear`

---

### `loop`

SYNOPSIS  
`loop <from> <to>`  
`loop`  
`loop clear`

DESCRIPTION  
Loops between two recorded frame numbers (decimal) in the state buffer. Both frames must exist in the state buffer.

NOTES  
The state buffer size is configurable via `E9K_STATE_BUFFER_BYTES` (see “Runtime Requirements”).

EXAMPLES  
`loop 120 180`  
`loop`  
`loop clear`

---

### `diff`

SYNOPSIS  
`diff <fromFrame> <toFrame> [size=8|16|32]`

DESCRIPTION  
Shows RAM addresses that differ between two recorded frames (state buffer), scanning:

- Main RAM (`0x00100000` .. `0x0010FFFF`)
- Backup RAM (`0x00D00000` .. `0x00D0FFFF`)

Output is truncated after 4096 lines.

EXAMPLES  
`diff 120 180`  
`diff 120 180 size=16`

---

### `transition`

SYNOPSIS  
`transition <slide|explode|doom|flip|rbar|random|cycle|none>`

DESCRIPTION  
Sets the transition mode used for startup and fullscreen transitions and persists it to the config.

EXAMPLES  
`transition random`  
`transition none`

---

### `base`
	
SYNOPSIS  
`base [text|data|bss] [addr|clear]`
`base clear`
	
DESCRIPTION  
Shows or sets the current runtime base address for each section (`text`, `data`, `bss`). These bases are used to translate between:
	
 - **Runtime addresses** (what the core uses / what you read/write in memory), and
 - **Debug/symbol addresses** (what external resolvers like `addr2line`/`readelf`/`objdump` expect)
   
In general:
 - `debug_addr = runtime_addr - <sectionBase>`
 - `runtime_addr = debug_addr + <sectionBase>`
   
This is required for relocatable images, and affects source/symbol resolution and operations like breakpoints and `print` that depend on debug info.
	
NOTES  
  - `addr` accepts decimal or `0x...`.
  - `base` values are per-session (not persisted).

EXAMPLES  
  `base`
  `base text 0x00C0FE24`
  `base data 0x00C11320`
  `base bss 0x00C1138C`
  `base text`
  `base bss clear`
  `base clear`
  
---

## Runtime Requirements

### Neo Geo BIOS ROMs (required)

BIOS files are not included. You must supply a valid Neo Geo BIOS set in the **system/BIOS directory**.

- Default system directory: `./system`
- Set via Settings UI: `BIOS FOLDER`
- Or via CLI: `--system-dir PATH` (use `--neogeo` to force Neo Geo mode)

In this repo, the default `./system` directory corresponds to `e9k-debugger/system` when running from the `e9k-debugger` directory. In practice this is typically a BIOS archive such as `neogeo.zip` (MVS / UniBIOS) or `aes.zip` (AES), placed inside the system directory.

### Game ROMS

`e9k-debugger` can load .neo files or attempt to automatically create a .neo file from a mame style rom set. 

- It uses two naming conventions:
  - the files with a rom specific file extension (.p1, .m1, .v1 etc)
  - the filename prior to the extension has a rom designation (rom-p1.bin)
- Use the ROM Folder to load mame style rom sets.

### Amiga Configuration

- Create an Amiga configuration by selecting "NEW" UAE file in the SETTINGS screen
- Amiga config is a combination of selecting PUAE core options (SETTIGNS->Core Options) and DF0/DF1/DH0 config on the settings screen
- Additional uae options can be manually added to the .uae file
- Not all PUAE core options make sense or will work (hotkeys for example) - I still haven't filtered these out
- Basic PUAE configs have been tested - large complex configs will not work well with the state saving buffer

### Amiga Kickstart ROMs (required)

- Kickstart ROMS are not included.
- A complete set of WHDLoad kickstart roms in your system folder is the best option.
- Otherwise manually settting kickstart roms in the .uae file is required.

### Emulator Platform defaults:

- macOS default core paths: `./system/geolith_libretro.dylib` for Neo Geo and `./system/puae_libretro.dylib` for Amiga
- Windows default core paths: `./system/geolith_libretro.dll` for Neo Geo and `./system/puae_libretro.dll` for Amiga

### Toolchain 

For C source-level stepping, symbol breakpoints, and rich `print` expressions:

Configure your toolchain for each platform in the settings screen. Currently tested:

- Neo Geo - ngdevkit `m68k-neogeo-elf`
- Amiga - bebbo's amiga-gcc `m68k-amigaos`

Without these, the debugger can still run, but symbol/source-aware features degrade or become unavailable.

#### Neo Geo
- An ELF compiled with DWARF debug info (`Settings → ELF`, or `--elf PATH`)
- The Neo Geo toolchain binaries on `PATH`:
  - `m68k-neogeo-elf-addr2line`
  - `m68k-neogeo-elf-objdump`
  - `m68k-neogeo-elf-readelf`

#### Amiga
- To use bebbo's toolchain please use https://github.com/AmigaPorts/m68k-amigaos-gcc - it contains important fixes to addr2line
- An hunk compiled with amiga-gcc debug info 
- The amiga-os-gcc binaries on `PATH`:
  - `m68k-amigaos-addr2line`
  - `m68k-amigaos-objdump`
  
Note: Amiga debugging is complicated by relocation. If your target application is relocated you must inform the debugger of the base address of each section. This can be done with either the `base` command or using the Amiga fake perphierals such that your Amiga loader can automatically inform the debugger of the base addresses. See "Amiga Fake Peripherals" section or "base" command documentation.

- An ELF image with dwarf information running on Amiga should technically work but is untested.

### Config file + environment variables

Config is persisted automatically (layout + settings):

- macOS: `~/.e9k-debugger.cfg`
- Windows: `%APPDATA%\\e9k-debugger.cfg` (falls back to `%USERPROFILE%`)

Useful environment variables:

- `E9K_STATE_BUFFER_BYTES`: state buffer capacity (default is `64*1024*1024`)
- `E9K_PROFILE_JSON`: output path for profiler analysis JSON (otherwise a temp file is used)

### Command-line options

Run `e9k-debugger --help` for the full list. The current options include:

#### Global options
- `--help`, `-h`
- `--reset-cfg` (deletes the saved config file and restarts)
- `--amiga`, `--neogeo` (sets the active system; affects which config options apply)
- `--core PATH` (applies to the active system)
- `--system-dir PATH` (applies to the active system)
- `--save-dir PATH` (applies to the active system)
- `--source-dir PATH` (applies to the active system)
- `--audio-buffer-ms MS` (currently Neo Geo only)
- `--window-size WxH`
- `--record PATH`, `--playback PATH`
- `--make-smoke PATH`, `--smoke-test PATH`, `--smoke-open`
- `--headless` - hide the main window (also disables rolling state recording by default)
- `--warp` - start in speed multiplier mode
- `--fullscreen` (alias: `--start-fullscreen`) - start in UI fullscreen mode (ESC toggle)
- `--no-rolling-record` - start with rolling state recording paused (can be toggled with `F11`; smoke/headless also pause by default)

#### Neo Geo options (use with `--neogeo`)
- `--elf PATH`
- `--rom PATH` (Neo Geo `.neo` file)
- `--rom-folder PATH` (generates a `.neo`)

#### Amiga options (use with `--amiga`)
- `--hunk PATH` (Amiga debug binary path)
- `--uae PATH` (Amiga UAE config `.uae` path)

---

## Copyright/License

e9k-debugger/ Copyright (C) 2026 Enable Software Pty Ltd

This project contains files with various licenses, unless otherwise specified assume GNU General Public License, version 2.

---

## Building

### macOS

- `make`

This should create
- `e9k-debugger/e9k-debugger` - macOS executable
- `e9k-debugger/system/puae_libretro.dll` - Amiga emulator core
- `e9k-debugger/system/geolith_libretro.dylib` - Neo Geo emulator core

- `e9k-debugger` links against at least: SDL2, SDL2_ttf, SDL2_image, readline, and OpenGL/Cocoa frameworks.
- The macOS build currently links sanitizers (`-fsanitize=address,undefined`) by default; adjust the Makefile if you want a non-sanitized release build.
- Typical Homebrew deps: `brew install sdl2 sdl2_image sdl2_ttf readline`

### Windows (MinGW, cross-compiling)

Windows builds use a `x86_64-w64-mingw32` toolchain and have so far only been tested by cross-compiling from macOS.

- `make w64`

This should create

- `e9k-debugger/dist/e9kd/e9k-debugger` - I create it here so can place all my .dll's for wine to find
- `e9k-debugger/system/geolith_libretro.dll` - I link `e9k-debugger/system` to `e9k-debugger/dist/e9kd/system`
- `e9k-debugger/system/puae_libretro.dll` - I link `e9k-debugger/system` to `e9k-debugger/dist/e9kd/system`

- You are likely to need to recreate my dist directory structure for the w64 build 
- I currently have dist/e9kd/ which contains the exe and any dll (SDL etc) used to link
- Inside that symlink to ../../assets and ../../system - you will also need a "saves" folder 
