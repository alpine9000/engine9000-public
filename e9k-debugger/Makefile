CC ?= cc
PKG_CONFIG ?= pkg-config
MINGW_HOME ?= /usr/local/mingw

INCLUDES += -I../geo9000/libretro
INCLUDES += -I../geo9000/src
INCLUDES += -I./e9ui
INCLUDES += -I./lib9000
INCLUDES += -I.

DEBUG_FLAGS=-g -fno-omit-frame-pointer -fsanitize=address,undefined 
CFLAGS += $(DEBUG_FLAGS) -Wall -Wextra -std=c99 -MMD -MP -Werror -pthread
CFLAGS += #-O2 -flto
CFLAGS += $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_image SDL2_ttf libpng)
CFLAGS += $(INCLUDES)

LIBS   += $(shell $(PKG_CONFIG) --libs sdl2 SDL2_image SDL2_ttf libpng)
LIBS   += -ldl
LIBS   += -fsanitize=address,undefined
LIBS   += -lreadline
LIBS   += -pthread
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LIBS   += -framework Cocoa
LIBS   += -framework OpenGL
endif

TARGET = e9k-debugger
BUILD_DIR = build
W64_CC ?= x86_64-w64-mingw32-gcc
W64_TARGET = dist/e9kd/e9k-debugger.exe
W64_BUILD_DIR = build-w64
W64_CFLAGS = -flto -O2 -Wall -Wextra -std=c99 -MMD -MP
W64_CFLAGS += $(INCLUDES)
W64_CFLAGS += -I$(MINGW_HOME)/include/SDL2 -I$(MINGW_HOME)/include -I$(MINGW_HOME)/include/readline -D_THREAD_SAFE
W64_CFLAGS += $(W64_EXTRA_CFLAGS)
W64_LIBS =
W64_LIBS += -L$(MINGW_HOME)/lib -lmingw32 -lSDL2main -lSDL2 -lSDL2_ttf -lSDL2_image -lreadline -lole32 -lcomdlg32 -lpng -lz -lopengl32
W64_LIBS += $(W64_EXTRA_LIBS)
W64_LDFLAGS = -s -flto -mconsole

BUNDLE_NAME = Engine9000.app
BUNDLE_DIR = $(BUNDLE_NAME)/Contents
BUNDLE_MACOS = $(BUNDLE_DIR)/MacOS
BUNDLE_RESOURCES = $(BUNDLE_DIR)/Resources

LIB9000_SRCS=alloc.c list.c resource.c file.c

E9UI_SRCS=e9ui_textbox.c e9ui_text_cache.c e9ui_text_select.c e9ui_text.c e9ui_scroll.c e9ui.c e9ui_split.c e9ui_split_stack.c e9ui_stack.c\
	  e9ui_image.c e9ui_box.c e9ui_hstack.c e9ui_button.c e9ui_checkbox.c e9ui_flow.c e9ui_header_flow.c e9ui_spacer.c e9ui_vspacer.c e9ui_separator.c\
          e9ui_badge.c e9ui_overlay.c e9ui_center.c e9ui_fileselect.c e9ui_labeled_textbox.c e9ui_labeled_checkbox.c e9ui_labeled_select.c e9ui_event.c \
	  e9ui_theme.c e9ui_link.c e9ui_modal.c e9ui_child.c 

SRCS = main.c source_pane.c source.c dasm.c dasm_geo.c dasm_ami.c elfutil.c addr2line.c console_cmd.c print_eval.c print_debuginfo_readelf.c print_debuginfo_readelf_frames.c print_debuginfo_objdump_stabs.c prompt.c  console.c registers.c memory.c stack.c breakpoints.c trainer.c linebuf.c analyse.c debug_font.c debug.c machine.c profile.c profile_view.c profile_list.c profile_hotspot.c profile_checkpoints.c input_record.c smoke_test.c  snapshot.c romset.c settings.c core_options.c core_config.c amiga_uae_options.c neogeo_core_options.c cli.c runtime.c config.c ui.c debugger.c help.c debugger_platform.c emu.c emu_geo.c emu_ami.c sprite_debug.c seek_bar.c libretro_host.c clipboard.c clipboard_osx.m state_buffer.c tinyfiledialogs.c signal.c transition.c transition_doom.c transition_slide.c transition_explode.c transition_flip.c transition_rbar.c crt.c shader_ui.c memory_track_ui.c shader_base.c shader_advanced.c shader_bloom.c gl_composite.c train.c protect.c status_bar.c

W64_SRCS = $(filter-out analyse.c profile_view.c signal.c debugger_platform.c clipboard_osx.m,$(SRCS)) w64_analyse.c w64_profile_view.c w64_signal.c w64_debugger_platform.c w64_clipboard.c $(addprefix e9ui/, $(E9UI_SRCS)) $(addprefix lib9000/, $(LIB9000_SRCS))
SRCS_C = $(filter %.c,$(SRCS)) $(addprefix e9ui/, $(E9UI_SRCS)) $(addprefix lib9000/, $(LIB9000_SRCS))
SRCS_M = $(filter %.m,$(SRCS))
OBJS = $(SRCS_C:%.c=$(BUILD_DIR)/%.o) $(SRCS_M:%.m=$(BUILD_DIR)/%.o)
DEPS = $(OBJS:.o=.d)
W64_OBJS = $(W64_SRCS:%.c=$(W64_BUILD_DIR)/%.o)
W64_DEPS = $(W64_OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

$(W64_TARGET): $(W64_OBJS)
	@mkdir -p $(dir $@)
	$(W64_CC) $(W64_CFLAGS) $(W64_LDFLAGS) -o $@ $(W64_OBJS) $(W64_LIBS)

w64: $(W64_TARGET)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: e9ui/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: lib9000/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.m
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(W64_BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(W64_CC) $(W64_CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(W64_TARGET) $(OBJS) $(DEPS) $(W64_OBJS) $(W64_DEPS)
	rm -rf $(BUILD_DIR) $(W64_BUILD_DIR)

bundle: $(TARGET)
	@mkdir -p $(BUNDLE_MACOS) $(BUNDLE_RESOURCES)
	@cp $(TARGET) $(BUNDLE_MACOS)/e9k-debugger
	@rm -rf $(BUNDLE_MACOS)/assets $(BUNDLE_MACOS)/system
	@cp -R assets $(BUNDLE_MACOS)/assets
	@cp -R dist/e9kd/system $(BUNDLE_MACOS)/system
	@cp assets/icons/osx/Engine9000.icns $(BUNDLE_RESOURCES)/Engine9000.icns
	@printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
		'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
		'<plist version="1.0">' \
		'<dict>' \
		'  <key>CFBundleName</key>' \
		'  <string>Engine9000</string>' \
		'  <key>CFBundleDisplayName</key>' \
		'  <string>Engine9000</string>' \
		'  <key>CFBundleIdentifier</key>' \
		'  <string>com.engine9000.debugger</string>' \
		'  <key>CFBundleExecutable</key>' \
		'  <string>e9k-debugger</string>' \
		'  <key>CFBundleIconFile</key>' \
		'  <string>Engine9000</string>' \
		'  <key>CFBundlePackageType</key>' \
		'  <string>APPL</string>' \
		'  <key>CFBundleVersion</key>' \
		'  <string>1.0</string>' \
		'  <key>CFBundleShortVersionString</key>' \
		'  <string>1.0</string>' \
		'</dict>' \
		'</plist>' > $(BUNDLE_DIR)/Info.plist

.PHONY: all clean macos-app

-include $(DEPS)
-include $(W64_DEPS)
