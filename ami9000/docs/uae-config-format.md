# `.uae` configuration files in `ami9000` (PUAE libretro)

This documents the **actual `.uae` format/behavior** as implemented by the `ami9000` PUAE libretro wrapper (not the full WinUAE feature set).

## What a `.uae` file is

When the core is launched with a content path ending in `.uae`, the file is treated as a **text configuration**. The core:

1. Generates a base configuration from the selected/auto model preset and core options
2. Appends lines from the content `.uae` file (with some filtering/overrides)
3. Optionally appends extra config files from the save directory
4. Parses the resulting configuration using UAE/WinUAE-style config parsing

The final generated configuration is printed in the core’s debug log.

## File syntax (as consumed by UAE config parsing)

- Plain text, line-oriented.
- Blank lines are ignored.
- Comment lines begin with `;` (after optional leading whitespace).
- Settings are `key=value` pairs.
- Leading/trailing whitespace around `key` and `value` is trimmed by the UAE config parser.
- Values may be quoted in general UAE configs, but **see the “PUAE wrapper quirks” section** below.

## Config file layering (precedence)

For a `.uae` file loaded as content, the effective configuration is built in this order:

1. **Model preset** (`uae_model_config`)
2. **Core-options-derived config** (`uae_config`)
3. **Content `.uae` file** (the file you loaded)
4. Optional: `retro_save_directory/puae_libretro_global.uae`
5. Optional: `retro_save_directory/[content_basename].uae`

Later entries win when the same key appears multiple times (duplicate keys may be applied in-order).

## PUAE wrapper quirks (important)

The libretro wrapper does extra processing when the content is a `.uae` file:

### Lines that are always skipped

To avoid conflicting input setups, these lines are ignored when reading the content `.uae` file:

- Anything containing `input.` where the line begins with `i`
- Anything containing `joyport` where the line begins with `j`

### Lines that may be ignored depending on core options

If the core option `opt_kickstart` is not `auto`, the wrapper ignores:

- `kickstart_rom_file=...`

If the core option `opt_model` is not `auto`, the wrapper ignores these keys (so the selected model preset stays authoritative):

- `kickstart_rom_file=...`
- `cpu_model=...`
- `fpu_model=...`
- `chipset=...`
- `chipset_compatible=...`
- `chipmem_size=...`
- `bogomem_size=...`
- `fastmem_size=...`
- `z3mem_size=...`

Additionally, if a corresponding core option override is set (e.g. `chipmem_size` not `auto`), the wrapper ignores the matching line in the `.uae` file even if `opt_model=auto`.

### Disk Control extraction from `.uae`

While reading the content `.uae`, the wrapper scans for disk image rows and builds the “Disk Control” playlist used for swapping.

It looks for lines containing **`diskimage`** or **`floppy`** (and starting with `d` or `f`), then takes the text after `=` as a candidate path. If that path exists, it is added to Disk Control.

Practical implications:

- Use **absolute paths** for `floppy*=` / `diskimage*=` entries (relative paths are not resolved relative to the `.uae` file’s directory).
- Do **not** wrap these paths in quotes, and avoid leading spaces after `=` (quotes/spaces will make the existence check fail, so the image won’t appear in Disk Control).

## Minimal examples

### Boot from a floppy image

```ini
; Minimal config to boot DF0:
kickstart_rom_file=/abs/path/to/kick34005.A500
floppy0=/abs/path/to/GameDisk.adf
```

### Mount a host directory as a hard drive

```ini
filesystem2=rw,DH0:Work:/abs/path/to/amiga-dir/,0
```

### Mount non-RDB HDFs (geometry specified)

```ini
hardfile=read-write,32,1,2,512,/abs/path/System.hdf
hardfile=read-write,32,2,2,512,/abs/path/WHDGamesA.hdf
```

## Where this behavior lives in code

- Content `.uae` handling: `ami9000/libretro/libretro-core.c` (inside `retro_create_config()`, the `.uae` branch)
- UAE config parsing rules (comments/whitespace/quotes): `ami9000/sources/src/cfgfile.c`
