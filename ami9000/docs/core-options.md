# PUAE core options (ami9000)

Concise reference for the PUAE libretro core options as defined in `ami9000/libretro/libretro-core.c` (US option table).

Notes:
- Keys are what frontends use internally (e.g. RetroArch config entries).
- Some options have their value lists populated dynamically at runtime (notably `puae_mapper_*` and `puae_cart_file`).
- Defaults may be conditional at compile time; those are shown explicitly when detected.

## System

Configure system options.

### Model
- Key: `puae_model`
- Menu path: System > Model
- Default: `auto`
- Description: 'Automatic' defaults to 'A500' with floppy disks, 'A1200' with hard drives and 'CD32' with compact discs. 'Automatic' can be overridden with file path tags. Core restart required.
- Values: (14 values; see frontend UI/source)

### Show Automatic Model Options
- Key: `puae_model_options_display`
- Menu path: System > Show Automatic Model Options
- Default: `disabled`
- Description: Show/hide default model options (Floppy/HD/CD) for 'Automatic' model.
- Values:
  - `disabled`
  - `enabled`

### Automatic Floppy
- Key: `puae_model_fd`
- Menu path: Model > Automatic Floppy
- Default: `A500`
- Description: Default model when floppies are launched with 'Automatic' model. Core restart required.
- Values:
  - `A500OG` — A500 (v1.3, 0.5M Chip)
  - `A500` — A500 (v1.3, 0.5M Chip + 0.5M Slow)
  - `A500PLUS` — A500+ (v2.04, 1M Chip)
  - `A600` — A600 (v3.1, 2M Chip + 8M Fast)
  - `A1200OG` — A1200 (v3.1, 2M Chip)
  - `A1200` — A1200 (v3.1, 2M Chip + 8M Fast)
  - `A2000OG` — A2000 (v1.3, 0.5M Chip + 0.5M Slow)
  - `A2000` — A2000 (v3.1, 1M Chip)
  - `A4030` — A4000/030 (v3.1, 2M Chip + 8M Fast)
  - `A4040` — A4000/040 (v3.1, 2M Chip + 8M Fast)

### Automatic HD
- Key: `puae_model_hd`
- Menu path: Model > Automatic HD
- Default: `A1200`
- Description: Default model when HD interface is used with 'Automatic' model. Affects WHDLoad installs and other hard drive images. Core restart required.
- Values:
  - `A600` — A600 (v3.1, 2M Chip + 8M Fast)
  - `A1200OG` — A1200 (v3.1, 2M Chip)
  - `A1200` — A1200 (v3.1, 2M Chip + 8M Fast)
  - `A2000` — A2000 (v3.1, 1M Chip)
  - `A4030` — A4000/030 (v3.1, 2M Chip + 8M Fast)
  - `A4040` — A4000/040 (v3.1, 2M Chip + 8M Fast)

### Automatic CD
- Key: `puae_model_cd`
- Menu path: Model > Automatic CD
- Default: `CD32`
- Description: Default model when compact discs are launched with 'Automatic' model. Core restart required.
- Values:
  - `CDTV` — CDTV (1M Chip)
  - `CD32` — CD32 (2M Chip)
  - `CD32FR` — CD32 (2M Chip + 8M Fast)

### Kickstart ROM
- Key: `puae_kickstart`
- Menu path: System > Kickstart ROM
- Default: `auto`
- Description: 'Automatic' defaults to the most compatible version for the model. 'AROS' is a built-in replacement with fair compatibility. Core restart required.
- Values: (13 values; see frontend UI/source)

### Chip RAM
- Key: `puae_chipmem_size`
- Menu path: System > Chip RAM
- Default: `auto`
- Description: 'Automatic' defaults to the current preset model. Core restart required.
- Values:
  - `auto` — Automatic
  - `1` — 0.5M
  - `2` — 1M
  - `3` — 1.5M
  - `4` — 2M

### Slow RAM
- Key: `puae_bogomem_size`
- Menu path: System > Slow RAM
- Default: `auto`
- Description: 'Automatic' defaults to the current preset model. Core restart required.
- Values:
  - `auto` — Automatic
  - `0` — None
  - `2` — 0.5M
  - `4` — 1M
  - `6` — 1.5M
  - `7` — 1.8M

### Z2 Fast RAM
- Key: `puae_fastmem_size`
- Menu path: System > Z2 Fast RAM
- Default: `auto`
- Description: 'Automatic' defaults to the current preset model. Core restart required.
- Values:
  - `auto` — Automatic
  - `0` — None
  - `1` — 1M
  - `2` — 2M
  - `4` — 4M
  - `8` — 8M

### Z3 Fast RAM
- Key: `puae_z3mem_size`
- Menu path: System > Z3 Fast RAM
- Default: `auto`
- Description: 'Automatic' defaults to the current preset model. Core restart required.
- Values:
  - `auto` — Automatic
  - `0` — None
  - `1` — 1M
  - `2` — 2M
  - `4` — 4M
  - `8` — 8M
  - `16` — 16M
  - `32` — 32M
  - `64` — 64M
  - `128` — 128M
  - `256` — 256M
  - `512` — 512M

### CPU Model
- Key: `puae_cpu_model`
- Menu path: System > CPU Model
- Default: `auto`
- Description: 'Automatic' defaults to the current preset model. Core restart required.
- Values:
  - `auto` — Automatic
  - `68000`
  - `68010`
  - `68020`
  - `68030`
  - `68040`
  - `68060`

### FPU Model
- Key: `puae_fpu_model`
- Menu path: System > FPU Model
- Default: `auto`
- Description: 'Automatic' defaults to the current preset model. Core restart required.
- Values:
  - `auto` — Automatic
  - `0` — None
  - `68881`
  - `68882`
  - `cpu` — CPU internal

### CPU Speed
- Key: `puae_cpu_throttle`
- Menu path: System > CPU Speed
- Default: `0.0`
- Description: Ignored with 'Cycle-exact'.
- Values: (20 values; see frontend UI/source)

### CPU Cycle-exact Speed
- Key: `puae_cpu_multiplier`
- Menu path: System > CPU Cycle-exact Speed
- Default: `0`
- Description: Applies only with 'Cycle-exact'.
- Values:
  - `0` — Default
  - `1` — 3.546895 MHz
  - `2` — 7.093790 MHz (A500)
  - `4` — 14.187580 MHz (A1200)
  - `8` — 28.375160 MHz
  - `10` — 35.468950 MHz
  - `12` — 42.562740 MHz
  - `16` — 56.750320 MHz

### CPU Compatibility
- Key: `puae_cpu_compatibility`
- Menu path: System > CPU Compatibility
- Default: conditional (#if defined(__x86_64__))
  - then: `memory`
  - else: `normal`
- Description: Some games have graphic and/or speed issues without 'Cycle-exact'. 'Cycle-exact' can be forced with '(CE)' file path tag. (DMA/Memory) is forced to (Full) with 68000.
- Values:
  - `normal` — Normal
  - `compatible` — More compatible
  - `memory` — Cycle-exact (DMA/Memory)
  - `exact` — Cycle-exact (Full)

## Media

Configure media options.

### Automatic Load Fast-Forward
- Key: `puae_autoloadfastforward`
- Menu path: Media > Automatic Load Fast-Forward
- Default: `disabled`
- Description: Toggle frontend fast-forward during media access if there is no audio output. Mutes 'Floppy Sound Emulation'.
- Values:
  - `disabled`
  - `enabled`
  - `fd` — Floppy disks only
  - `hd` — Hard drives only
  - `cd` — Compact discs only

### Floppy Speed
- Key: `puae_floppy_speed`
- Menu path: Media > Floppy Speed
- Default: `100`
- Description: Default speed is 300RPM. 'Turbo' removes disk rotation emulation.
- Values:
  - `100` — Default
  - `200` — 2x
  - `400` — 4x
  - `800` — 8x
  - `0` — Turbo

### Floppy MultiDrive
- Key: `puae_floppy_multidrive`
- Menu path: Media > Floppy MultiDrive
- Default: `enabled`
- Description: Insert each disk in different drives. Can be forced with '(MD)' file path tag. Maximum is 4 disks due to external drive limit! Not all games support external drives! Core restart required.
- Values:
  - `disabled`
  - `enabled`

### Floppy Write Protection
- Key: `puae_floppy_write_protection`
- Menu path: Media > Floppy Write Protection
- Default: `disabled`
- Description: Set all drives read only. Changing this while emulation is running ejects and reinserts all disks. IPF images are always read-only!
- Values:
  - `disabled`
  - `enabled`

### Floppy Write Redirect
- Key: `puae_floppy_write_redirect`
- Menu path: Media > Floppy Write Redirect
- Default: `disabled`
- Description: Writes to a substitute disk under 'saves' instead of original disks. Works also with IPF images.
- Values:
  - `disabled`
  - `enabled`

### CD Speed
- Key: `puae_cd_speed`
- Menu path: Media > CD Speed
- Default: `100`
- Description: Transfer rate in CD32 is 300KB/s (double-speed), CDTV is 150KB/s (single-speed). 'Turbo' removes seek delay emulation.
- Values:
  - `100` — Default
  - `0` — Turbo

### CD Startup Delayed Insert
- Key: `puae_cd_startup_delayed_insert`
- Menu path: Media > CD Startup Delayed Insert
- Default: `disabled`
- Description: Some games fail to load if CD32/CDTV is powered on with CD inserted. 'ON' inserts CD during boot animation.
- Values:
  - `disabled`
  - `enabled`

### CD32/CDTV Shared NVRAM
- Key: `puae_shared_nvram`
- Menu path: Media > CD32/CDTV Shared NVRAM
- Default: `disabled`
- Description: 'OFF' saves separate files per content. Starting without content uses the shared file. CD32 and CDTV use separate shared files. Core restart required.
- Values:
  - `disabled`
  - `enabled`

### WHDLoad Support
- Key: `puae_use_whdload`
- Menu path: Media > WHDLoad Support
- Default: `files`
- Description: Enable launching pre-installed WHDLoad installs. Creates a helper boot image for loading content and an empty image for saving. Legacy 'HDFs' mode is not recommended! Core restart required. - 'Files' creates data in directories - 'HDFs' creates data in images - 'OFF' boots hard drive images directly
- Values:
  - `disabled`
  - `files` — Files
  - `hdfs` — HDFs

### WHDLoad Theme
- Key: `puae_use_whdload_theme`
- Menu path: Media > WHDLoad Theme
- Default: `default`
- Description: AmigaOS 'system-configuration' color prefs in WHDLoad helper image. Available only with 'Files' mode. Core restart required. - 'Default' = Black/White/DarkGray/LightGray - 'Native' = Gray/Black/White/LightBlue
- Values:
  - `default` — Default
  - `native` — Native

### WHDLoad Splash Screen
- Key: `puae_use_whdload_prefs`
- Menu path: Media > WHDLoad Splash Screen
- Default: `disabled`
- Description: Space/Enter/Fire works as WHDLoad Start-button. Core restart required. Override with buttons while booting: - 'Config': Hold 2nd fire / Blue - 'Splash': Hold LMB - 'Config + Splash': Hold RMB - ReadMe + MkCustom: Hold Red+Blue
- Values:
  - `disabled`
  - `config` — Config (Show only if available)
  - `splash` — Splash (Show briefly)
  - `both` — Config + Splash (Wait for user input)

### WHDLoad ButtonWait
- Key: `puae_use_whdload_buttonwait`
- Menu path: Media > WHDLoad ButtonWait
- Default: `enabled`
- Description: Wait for a button press on internal loading sections if the slave supports it. Core restart required.
- Values:
  - `disabled`
  - `enabled`

### WHDLoad NoWriteCache
- Key: `puae_use_whdload_nowritecache`
- Menu path: Media > WHDLoad NoWriteCache
- Default: `disabled`
- Description: Write cache requires running the core a few frames after closing content to trigger WHDLoad quit and flush cache to disk. QuitKey = '$2b' = '#' = 'LCtrl + Backslash'. Core restart required.
- Values:
  - `disabled`
  - `enabled`

### Global Boot HD
- Key: `puae_use_boot_hd`
- Menu path: Media > Global Boot HD
- Default: `disabled`
- Description: Attach a hard disk meant for Workbench, not for WHDLoad! Enabling forces a model with HD interface. Changing HDF size will not replace or edit the existing HDF. Core restart required.
- Values:
  - `disabled`
  - `files` — Files
  - `hdf20` — HDF 20MB
  - `hdf40` — HDF 40MB
  - `hdf80` — HDF 80MB
  - `hdf128` — HDF 128MB
  - `hdf256` — HDF 256MB
  - `hdf512` — HDF 512MB

### Cartridge
- Key: `puae_cart_file`
- Menu path: Media > Cartridge
- Default: (none)
- Values: populated dynamically from available cartridge definitions

## Uncategorized

### Show Video Options
- Key: `puae_video_options_display`
- Menu path: Show Video Options
- Default: `disabled`
- Values:
  - `disabled`
  - `enabled`

### Show Audio Options
- Key: `puae_audio_options_display`
- Menu path: Show Audio Options
- Default: `disabled`
- Values:
  - `disabled`
  - `enabled`

### Show Mapping Options
- Key: `puae_mapping_options_display`
- Menu path: Show Mapping Options
- Default: `enabled`
- Values:
  - `disabled`
  - `enabled`

## Video

Configure video options.

### Allow Hz Change
- Key: `puae_video_allow_hz_change`
- Menu path: Video > Allow Hz Change
- Default: `locked`
- Description: Let Amiga decide the exact refresh rate when interlace mode or PAL/NTSC changes. 'Locked' changes only when video standard changes. Core restart required.
- Values:
  - `disabled`
  - `enabled`
  - `locked` — Locked PAL/NTSC

### Standard
- Key: `puae_video_standard`
- Menu path: Video > Standard
- Default: `PAL auto`
- Description: Output Hz & height: - 'PAL': 50Hz - 288px / 576px - 'NTSC': 60Hz - 240px / 480px - 'Automatic' switches region per file path tags.
- Values:
  - `PAL auto` — Automatic PAL
  - `NTSC auto` — Automatic NTSC
  - `PAL`
  - `NTSC`

### Pixel Aspect Ratio
- Key: `puae_video_aspect`
- Menu path: Video > Pixel Aspect Ratio
- Default: `auto`
- Description: Hotkey toggling disables this option until core restart. - 'PAL': 26/25 = 1.04 - 'NTSC': 43/50 = 0.86
- Values:
  - `auto` — Automatic
  - `PAL`
  - `NTSC`
  - `1:1`

### Resolution
- Key: `puae_video_resolution`
- Menu path: Video > Resolution
- Default: `auto`
- Description: Output width: - 'Automatic' uses 'High' at minimum. - 'Automatic (Low)' allows 'Low'. - 'Automatic (Super-High)' sets max size already at startup.
- Values:
  - `auto` — Automatic
  - `auto-lores` — Automatic (Low)
  - `auto-superhires` — Automatic (Super-High)
  - `lores` — Low 360px
  - `hires` — High 720px
  - `superhires` — Super-High 1440px

### Line Mode
- Key: `puae_video_vresolution`
- Menu path: Video > Line Mode
- Default: `auto`
- Description: Output height: - 'Automatic' defaults to 'Single Line' and switches to 'Double Line' on interlaced screens.
- Values:
  - `auto` — Automatic
  - `single` — Single Line
  - `double` — Double Line

### Crop
- Key: `puae_crop`
- Menu path: Video > Crop
- Default: `disabled`
- Description: Remove borders according to 'Crop Mode'.
- Values:
  - `disabled`
  - `minimum` — Minimum
  - `smaller` — Smaller
  - `small` — Small
  - `medium` — Medium
  - `large` — Large
  - `larger` — Larger
  - `maximum` — Maximum
  - `auto` — Automatic

### Automatic Crop Delay
- Key: `puae_crop_delay`
- Menu path: Video > Automatic Crop Delay
- Default: `enabled`
- Description: Patient or instant geometry change.
- Values:
  - `disabled`
  - `enabled`

### Crop Mode
- Key: `puae_crop_mode`
- Menu path: Video > Crop Mode
- Default: `both`
- Description: 'Horizontal + Vertical' & 'Automatic' removes borders completely.
- Values:
  - `both` — Horizontal + Vertical
  - `horizontal` — Horizontal
  - `vertical` — Vertical
  - `16:9`
  - `16:10`
  - `4:3`
  - `5:4`

### Vertical Position
- Key: `puae_vertical_pos`
- Menu path: Video > Vertical Position
- Default: `auto`
- Description: 'Automatic' keeps only cropped screens centered. Positive values move upward and negative values move downward.
- Values: (47 values; see frontend UI/source)

### Horizontal Position
- Key: `puae_horizontal_pos`
- Menu path: Video > Horizontal Position
- Default: `auto`
- Description: 'Automatic' keeps screen centered. Positive values move right and negative values move left.
- Values: (42 values; see frontend UI/source)

### Immediate/Waiting Blits
- Key: `puae_immediate_blits`
- Menu path: Video > Immediate/Waiting Blits
- Default: `waiting`
- Description: - 'Immediate Blitter': Faster but less compatible blitter emulation. - 'Wait for Blitter': Compatibility hack for programs that don't wait for the blitter correctly.
- Values:
  - `false` — disabled
  - `immediate` — Immediate Blitter
  - `waiting` — Wait for Blitter

### Collision Level
- Key: `puae_collision_level`
- Menu path: Video > Collision Level
- Default: `playfields`
- Description: 'Sprites and Playfields' is recommended.
- Values:
  - `none` — None
  - `sprites` — Sprites only
  - `playfields` — Sprites and Playfields
  - `full` — Full

### Remove Interlace Artifacts
- Key: `puae_gfx_flickerfixer`
- Menu path: Video > Remove Interlace Artifacts
- Default: `disabled`
- Description: Best suited for still screens, Workbench etc.
- Values:
  - `disabled`
  - `enabled`

### Frameskip
- Key: `puae_gfx_framerate`
- Menu path: Video > Frameskip
- Default: `disabled`
- Description: Not compatible with 'Cycle-exact'.
- Values:
  - `disabled`
  - `1`
  - `2`

### Color Gamma
- Key: `puae_gfx_gamma`
- Menu path: Video > Color Gamma
- Default: `0`
- Description: Adjust color gamma.
- Values:
  - `-500` — +0.5
  - `-400` — +0.4
  - `-300` — +0.3
  - `-200` — +0.2
  - `-100` — +0.1
  - `0` — disabled
  - `100` — -0.1
  - `200` — -0.2
  - `300` — -0.3
  - `400` — -0.4
  - `500` — -0.5

### Color Depth
- Key: `puae_gfx_colors`
- Menu path: Video > Color Depth
- Default: conditional (#if defined(VITA) || defined(__SWITCH__) || defined(DINGUX) || defined(ANDROID))
  - then: `16bit`
  - else: `24bit`
- Description: Full restart required.
- Values:
  - `16bit` — 16-bit (RGB565)
  - `24bit` — 24-bit (XRGB8888)

## On-Screen Display

Configure OSD options.

### Virtual KBD Theme
- Key: `puae_vkbd_theme`
- Menu path: OSD > Virtual KBD Theme
- Default: `auto`
- Description: The keyboard comes up with RetroPad Select by default.
- Values:
  - `auto` — Automatic (shadow)
  - `auto_outline` — Automatic (outline)
  - `beige` — Beige (shadow)
  - `beige_outline` — Beige (outline)
  - `cd32` — CD32 (shadow)
  - `cd32_outline` — CD32 (outline)
  - `light` — Light (shadow)
  - `light_outline` — Light (outline)
  - `dark` — Dark (shadow)
  - `dark_outline` — Dark (outline)

### Virtual KBD Transparency
- Key: `puae_vkbd_transparency`
- Menu path: OSD > Virtual KBD Transparency
- Default: `25%`
- Description: Keyboard transparency can be toggled with RetroPad A.
- Values:
  - `0%`
  - `25%`
  - `50%`
  - `75%`
  - `100%`

### Virtual KBD Dimming
- Key: `puae_vkbd_dimming`
- Menu path: OSD > Virtual KBD Dimming
- Default: `25%`
- Description: Dimming level of the surrounding area.
- Values:
  - `0%`
  - `25%`
  - `50%`
  - `75%`
  - `100%`

### Statusbar Mode
- Key: `puae_statusbar`
- Menu path: OSD > Statusbar Mode
- Default: `bottom`
- Description: - 'Full': Joyports + Messages + LEDs - 'Basic': Messages + LEDs - 'Minimal': LED colors only
- Values:
  - `bottom` — Bottom Full
  - `bottom_minimal` — Bottom Full Minimal
  - `bottom_basic` — Bottom Basic
  - `bottom_basic_minimal` — Bottom Basic Minimal
  - `top` — Top Full
  - `top_minimal` — Top Full Minimal
  - `top_basic` — Top Basic
  - `top_basic_minimal` — Top Basic Minimal

### Statusbar Startup
- Key: `puae_statusbar_startup`
- Menu path: OSD > Statusbar Startup
- Default: `disabled`
- Description: Show statusbar on startup.
- Values:
  - `disabled`
  - `enabled`

### Statusbar Messages
- Key: `puae_statusbar_messages`
- Menu path: OSD > Statusbar Messages
- Default: `disabled`
- Description: Show messages when statusbar is hidden.
- Values:
  - `disabled`
  - `enabled`

### Light Pen/Gun Pointer Color
- Key: `puae_joyport_pointer_color`
- Menu path: OSD > Light Pen/Gun Pointer Color
- Default: `blue`
- Description: Crosshair color for light pens and guns.
- Values:
  - `disabled`
  - `black` — Black
  - `white` — White
  - `red` — Red
  - `green` — Green
  - `blue` — Blue
  - `yellow` — Yellow
  - `purple` — Purple

## Audio

Configure audio options.

### Stereo Separation
- Key: `puae_sound_stereo_separation`
- Menu path: Audio > Stereo Separation
- Default: `100%`
- Description: Paula sound chip channel panning. Does not affect CD audio.
- Values:
  - `0%`
  - `10%`
  - `20%`
  - `30%`
  - `40%`
  - `50%`
  - `60%`
  - `70%`
  - `80%`
  - `90%`
  - `100%`

### Interpolation
- Key: `puae_sound_interpol`
- Menu path: Audio > Interpolation
- Default: `anti`
- Description: Paula sound chip interpolation type.
- Values:
  - `none` — None
  - `anti` — Anti
  - `sinc` — Sinc
  - `rh` — RH
  - `crux` — Crux

### Filter
- Key: `puae_sound_filter`
- Menu path: Audio > Filter
- Default: `emulated`
- Description: 'Emulated' allows states between ON/OFF.
- Values:
  - `emulated` — Emulated
  - `off` — Off
  - `on` — On

### Filter Type
- Key: `puae_sound_filter_type`
- Menu path: Audio > Filter Type
- Default: `auto`
- Description: 'Automatic' picks the filter type for the hardware.
- Values:
  - `auto` — Automatic
  - `standard` — A500
  - `enhanced` — A1200

### Floppy Sound Emulation
- Key: `puae_floppy_sound`
- Menu path: Audio > Floppy Sound Emulation
- Default: `80`
- Description: Floppy volume in percent.
- Values: (21 values; see frontend UI/source)

### Floppy Sound Mute Ejected
- Key: `puae_floppy_sound_empty_mute`
- Menu path: Audio > Floppy Sound Mute Ejected
- Default: `enabled`
- Description: Mute drive head clicking when floppy is not inserted.
- Values:
  - `disabled`
  - `enabled`

### Floppy Sound Type
- Key: `puae_floppy_sound_type`
- Menu path: Audio > Floppy Sound Type
- Default: `internal`
- Description: External files go in 'system/uae/' or 'system/uae_data/'.
- Values:
  - `internal` — Internal
  - `A500` — External: A500
  - `LOUD` — External: LOUD

### CD Audio Volume
- Key: `puae_sound_volume_cd`
- Menu path: Audio > CD Audio Volume
- Default: `100%`
- Description: CD volume in percent.
- Values: (21 values; see frontend UI/source)

## Input

Configure input options.

### Analog Stick Mouse
- Key: `puae_analogmouse`
- Menu path: Input > Analog Stick Mouse
- Default: `both`
- Description: Default mouse control stick when remappings are empty.
- Values:
  - `disabled`
  - `left` — Left Analog
  - `right` — Right Analog
  - `both` — Both Analogs

### Analog Stick Mouse Deadzone
- Key: `puae_analogmouse_deadzone`
- Menu path: Input > Analog Stick Mouse Deadzone
- Default: `20`
- Description: Required distance from stick center to register input.
- Values:
  - `0` — 0%
  - `5` — 5%
  - `10` — 10%
  - `15` — 15%
  - `20` — 20%
  - `25` — 25%
  - `30` — 30%
  - `35` — 35%
  - `40` — 40%
  - `45` — 45%
  - `50` — 50%

### Left Analog Stick Mouse Speed
- Key: `puae_analogmouse_speed`
- Menu path: Input > Left Analog Stick Mouse Speed
- Default: `1.0`
- Description: Mouse speed for left analog stick.
- Values: (30 values; see frontend UI/source)

### Right Analog Stick Mouse Speed
- Key: `puae_analogmouse_speed_right`
- Menu path: Input > Right Analog Stick Mouse Speed
- Default: `1.0`
- Description: Mouse speed for right analog stick.
- Values: (30 values; see frontend UI/source)

### D-Pad Mouse Speed
- Key: `puae_dpadmouse_speed`
- Menu path: Input > D-Pad Mouse Speed
- Default: `6`
- Description: Mouse speed for directional pad.
- Values: (19 values; see frontend UI/source)

### Mouse Speed
- Key: `puae_mouse_speed`
- Menu path: Input > Mouse Speed
- Default: `100`
- Description: Global mouse speed.
- Values: (30 values; see frontend UI/source)

### Physical Mouse
- Key: `puae_physicalmouse`
- Menu path: Input > Physical Mouse
- Default: `enabled`
- Description: 'Double' requirements: raw/udev input driver and proper mouse index per port. Does not affect RetroPad emulated mice.
- Values:
  - `disabled`
  - `enabled`
  - `double` — Double

### Keyboard Pass-through
- Key: `puae_physical_keyboard_pass_through`
- Menu path: Input > Keyboard Pass-through
- Default: `disabled`
- Description: 'ON' passes all physical keyboard events to the core. 'OFF' prevents RetroPad keys from generating keyboard events. NOTE: This is a legacy option for old frontends that do not block keyboard events when using RetroPad, so it does nothing with current RetroArch.
- Values:
  - `disabled`
  - `enabled`

### Keyrah Keypad Mappings
- Key: `puae_keyrah_keypad_mappings`
- Menu path: Input > Keyrah Keypad Mappings
- Default: `disabled`
- Description: Hardcoded keypad to joyport mappings for Keyrah hardware.
- Values:
  - `disabled`
  - `enabled`

### Turbo Fire
- Key: `puae_turbo_fire`
- Menu path: RetroPad > Turbo Fire
- Default: `disabled`
- Description: Hotkey toggling disables this option until core restart.
- Values:
  - `disabled`
  - `enabled`

### Turbo Button
- Key: `puae_turbo_fire_button`
- Menu path: RetroPad > Turbo Button
- Default: `B`
- Description: Replace the mapped button with turbo fire button.
- Values:
  - `B` — RetroPad B
  - `A` — RetroPad A
  - `Y` — RetroPad Y
  - `X` — RetroPad X
  - `L` — RetroPad L
  - `R` — RetroPad R
  - `L2` — RetroPad L2
  - `R2` — RetroPad R2

### Turbo Pulse
- Key: `puae_turbo_pulse`
- Menu path: RetroPad > Turbo Pulse
- Default: `6`
- Description: Frames in a button cycle. - '2' = 1 frame down, 1 frame up - '4' = 2 frames down, 2 frames up - '6' = 3 frames down, 3 frames up etc.
- Values:
  - `2` — 2 frames
  - `4` — 4 frames
  - `6` — 6 frames
  - `8` — 8 frames
  - `10` — 10 frames
  - `12` — 12 frames

### Joystick/Mouse
- Key: `puae_joyport`
- Menu path: RetroPad > Joystick/Mouse
- Default: `Joystick`
- Description: Change D-Pad control between joyports. Hotkey toggling disables this option until core restart.
- Values:
  - `joystick` — Joystick (Port 1)
  - `mouse` — Mouse (Port 2)

### Joystick Port Order
- Key: `puae_joyport_order`
- Menu path: RetroPad > Joystick Port Order
- Default: `1234`
- Description: Plug RetroPads in different ports. Useful for 4-player adapters.
- Values:
  - `1234` — 1-2-3-4
  - `2143` — 2-1-4-3
  - `3412` — 3-4-1-2
  - `4321` — 4-3-2-1

### RetroPad Face Button Options
- Key: `puae_retropad_options`
- Menu path: RetroPad > Face Button Options
- Default: `disabled`
- Description: Rotate face buttons clockwise and/or make 2nd fire press up.
- Values:
  - `disabled` — B = Fire, A = 2nd fire
  - `jump` — B = Fire, A = Up
  - `rotate` — Y = Fire, B = 2nd fire
  - `rotate_jump` — Y = Fire, B = Up

### CD32 Pad Face Button Options
- Key: `puae_cd32pad_options`
- Menu path: CD32 Pad > Face Button Options
- Default: `disabled`
- Description: Rotate face buttons clockwise and/or make blue button press up.
- Values:
  - `disabled` — B = Red, A = Blue
  - `jump` — B = Red, A = Up
  - `rotate` — Y = Red, B = Blue
  - `rotate_jump` — Y = Red, B = Up

## Hotkey Mapping

Configure keyboard hotkey mapping options.

### Toggle Virtual Keyboard
- Key: `puae_mapper_vkbd`
- Menu path: Hotkey > Toggle Virtual Keyboard
- Default: `---`
- Description: Press the mapped key to toggle the virtual keyboard.
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Toggle Statusbar
- Key: `puae_mapper_statusbar`
- Menu path: Hotkey > Toggle Statusbar
- Default: `RETROK_F12`
- Description: Press the mapped key to toggle the statusbar.
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Switch Joystick/Mouse
- Key: `puae_mapper_mouse_toggle`
- Menu path: Hotkey > Switch Joystick/Mouse
- Default: `RETROK_RCTRL`
- Description: Press the mapped key to switch between joystick and mouse control.
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Toggle Turbo Fire
- Key: `puae_mapper_turbo_fire_toggle`
- Menu path: Hotkey > Toggle Turbo Fire
- Default: `---`
- Description: Press the mapped key to toggle turbo fire.
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Toggle Save Disk
- Key: `puae_mapper_save_disk_toggle`
- Menu path: Hotkey > Toggle Save Disk
- Default: `---`
- Description: Press the mapped key to create/insert/eject a save disk.
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Toggle Aspect Ratio
- Key: `puae_mapper_aspect_ratio_toggle`
- Menu path: Hotkey > Toggle Aspect Ratio
- Default: `---`
- Description: Press the mapped key to toggle between PAL/NTSC pixel aspect ratio.
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Toggle Crop
- Key: `puae_mapper_crop_toggle`
- Menu path: Hotkey > Toggle Crop
- Default: `---`
- Description: Press the mapped key to toggle crop.
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Reset
- Key: `puae_mapper_reset`
- Menu path: Hotkey > Reset
- Default: `---`
- Description: Press the mapped key to trigger soft reset (Ctrl-Amiga-Amiga).
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

## RetroPad Mapping

Configure RetroPad mapping options.

### Up
- Key: `puae_mapper_up`
- Menu path: RetroPad > Up
- Default: `---`
- Description: Unmapped defaults to joystick up.
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Down
- Key: `puae_mapper_down`
- Menu path: RetroPad > Down
- Default: `---`
- Description: Unmapped defaults to joystick down.
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Left
- Key: `puae_mapper_left`
- Menu path: RetroPad > Left
- Default: `---`
- Description: Unmapped defaults to joystick left.
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Right
- Key: `puae_mapper_right`
- Menu path: RetroPad > Right
- Default: `---`
- Description: Unmapped defaults to joystick right.
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### B
- Key: `puae_mapper_b`
- Menu path: RetroPad > B
- Default: `---`
- Description: Unmapped defaults to fire button. VKBD: Press selected key.
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### A
- Key: `puae_mapper_a`
- Menu path: RetroPad > A
- Default: `---`
- Description: Unmapped defaults to 2nd fire button. VKBD: Toggle transparency. Remapping to non-keyboard keys overrides VKBD function!
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Y
- Key: `puae_mapper_y`
- Menu path: RetroPad > Y
- Default: `---`
- Description: VKBD: Toggle 'CapsLock'. Remapping to non-keyboard keys overrides VKBD function!
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### X
- Key: `puae_mapper_x`
- Menu path: RetroPad > X
- Default: `RETROK_SPACE`
- Description: VKBD: Press 'Space'. Remapping to non-keyboard keys overrides VKBD function!
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Select
- Key: `puae_mapper_select`
- Menu path: RetroPad > Select
- Default: `TOGGLE_VKBD`
- Description: VKBD comes up with RetroPad Select by default.
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Start
- Key: `puae_mapper_start`
- Menu path: RetroPad > Start
- Default: `---`
- Description: VKBD: Press 'Return'. Remapping to non-keyboard keys overrides VKBD function!
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### L
- Key: `puae_mapper_l`
- Menu path: RetroPad > L
- Default: ``
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### R
- Key: `puae_mapper_r`
- Menu path: RetroPad > R
- Default: ``
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### L2
- Key: `puae_mapper_l2`
- Menu path: RetroPad > L2
- Default: `MOUSE_LEFT_BUTTON`
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### R2
- Key: `puae_mapper_r2`
- Menu path: RetroPad > R2
- Default: `MOUSE_RIGHT_BUTTON`
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### L3
- Key: `puae_mapper_l3`
- Menu path: RetroPad > L3
- Default: `---`
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### R3
- Key: `puae_mapper_r3`
- Menu path: RetroPad > R3
- Default: `---`
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Left Analog > Up
- Key: `puae_mapper_lu`
- Menu path: RetroPad > Left Analog > Up
- Default: `---`
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Left Analog > Down
- Key: `puae_mapper_ld`
- Menu path: RetroPad > Left Analog > Down
- Default: `---`
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Left Analog > Left
- Key: `puae_mapper_ll`
- Menu path: RetroPad > Left Analog > Left
- Default: `---`
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Left Analog > Right
- Key: `puae_mapper_lr`
- Menu path: RetroPad > Left Analog > Right
- Default: `---`
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Right Analog > Up
- Key: `puae_mapper_ru`
- Menu path: RetroPad > Right Analog > Up
- Default: `---`
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Right Analog > Down
- Key: `puae_mapper_rd`
- Menu path: RetroPad > Right Analog > Down
- Default: `---`
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Right Analog > Left
- Key: `puae_mapper_rl`
- Menu path: RetroPad > Right Analog > Left
- Default: `---`
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)

### Right Analog > Right
- Key: `puae_mapper_rr`
- Menu path: RetroPad > Right Analog > Right
- Default: `---`
- Values: populated dynamically (keyboard key constants like `RETROK_*`, plus `---` for disabled)
