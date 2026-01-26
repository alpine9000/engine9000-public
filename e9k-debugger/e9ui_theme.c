/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL_ttf.h>
#include <limits.h>

#include "e9ui_theme.h"
#include "e9ui_theme_defaults.h"
#include "e9ui_text_cache.h"
#include "debugger.h"
#include "file.h"
#include "debug.h"

static const e9k_theme_button_t kThemeButtonRed = {
    .mask = E9K_THEME_BUTTON_MASK_HIGHLIGHT |
            E9K_THEME_BUTTON_MASK_BACKGROUND |
            E9K_THEME_BUTTON_MASK_PRESSED |
            E9K_THEME_BUTTON_MASK_SHADOW,
    .background = (SDL_Color){0xC6, 0x28, 0x28, 0xFF},
    .pressedBackground = (SDL_Color){0xA6, 0x08, 0x08, 0xFF},
    .highlight = (SDL_Color){0xE6, 0x4C, 0x4C, 0xFF},
    .shadow = (SDL_Color){0x6D, 0x1C, 0x1C, 0xFF}
};

static const e9k_theme_button_t kThemeButtonGreen = {
    .mask = E9K_THEME_BUTTON_MASK_HIGHLIGHT |
            E9K_THEME_BUTTON_MASK_BACKGROUND |
            E9K_THEME_BUTTON_MASK_PRESSED |
            E9K_THEME_BUTTON_MASK_SHADOW,
    .background = (SDL_Color){0x1B, 0x8F, 0x3A, 0xFF},
    .pressedBackground = (SDL_Color){0x13, 0x6F, 0x2D, 0xFF},
    .highlight = (SDL_Color){0x3D, 0xB5, 0x59, 0xFF},
    .shadow = (SDL_Color){0x0D, 0x4F, 0x1F, 0xFF}
};

static const e9k_theme_button_t kThemeButtonProfileActive = {
    .mask = E9K_THEME_BUTTON_MASK_HIGHLIGHT |
            E9K_THEME_BUTTON_MASK_BACKGROUND |
            E9K_THEME_BUTTON_MASK_PRESSED |
            E9K_THEME_BUTTON_MASK_SHADOW,
    .highlight = (SDL_Color){0x71, 0x9E, 0xF2, 0xFF},
    .background = (SDL_Color){0x2C, 0x63, 0xD2, 0xFF},
    .pressedBackground = (SDL_Color){0x1E, 0x47, 0xA8, 0xFF},
    .shadow = (SDL_Color){0x1A, 0x2C, 0x5A, 0xFF}
};

const e9k_theme_button_t *
e9ui_theme_button_preset_red(void)
{
    return &kThemeButtonRed;
}

const e9k_theme_button_t *
e9ui_theme_button_preset_green(void)
{
    return &kThemeButtonGreen;
}

const e9k_theme_button_t *
e9ui_theme_button_preset_profile_active(void)
{
    return &kThemeButtonProfileActive;
}

static int
e9ui_theme_scaledSize(int baseSize)
{
    if (baseSize <= 0) {
        return 1;
    }
    float scale = debugger.ui.ctx.dpiScale;
    if (scale <= 1.0f) {
        return baseSize;
    }
    int scaled = (int)(baseSize * scale + 0.5f);
    return scaled > 0 ? scaled : 1;
}

static TTF_Font *
e9ui_theme_openFontAsset(const char *asset, const char *fallback, int size, int style)
{
    const char *useAsset = (asset && *asset) ? asset : fallback;
    if (!useAsset || !*useAsset) {
        return NULL;
    }
    char path[PATH_MAX];
    if (!file_getAssetPath(useAsset, path, sizeof(path))) {
        debug_error("Theme: could not resolve font path %s", useAsset);
        return NULL;
    }
    TTF_Font *font = TTF_OpenFont(path, size);
    if (!font) {
        debug_error("Failed to load font at %s", path);
        return NULL;
    }
    if (style != TTF_STYLE_NORMAL) {
        TTF_SetFontStyle(font, style);
    }
    return font;
}



void
e9ui_theme_loadFonts(void)
{
    // Button font
    if (debugger.theme.button.font) {
        TTF_CloseFont(debugger.theme.button.font);
        debugger.theme.button.font = NULL;
    }
    int baseButton = debugger.theme.button.fontSize > 0 ? debugger.theme.button.fontSize : 18;
    int bsize = e9ui_theme_scaledSize(baseButton);
    debugger.theme.button.font = e9ui_theme_openFontAsset(debugger.theme.button.fontAsset,
                                                          E9UI_THEME_BUTTON_FONT_ASSET,
                                                          bsize,
                                                          debugger.theme.button.fontStyle);
    // Mini button font
    if (debugger.theme.miniButton.font) {
        TTF_CloseFont(debugger.theme.miniButton.font);
        debugger.theme.miniButton.font = NULL;
    }
    int baseMini = debugger.theme.miniButton.fontSize > 0 ? debugger.theme.miniButton.fontSize : baseButton;
    int msize = e9ui_theme_scaledSize(baseMini);
    const char *miniFallback = debugger.theme.button.fontAsset ? debugger.theme.button.fontAsset : E9UI_THEME_MINI_BUTTON_FONT_ASSET;
    debugger.theme.miniButton.font = e9ui_theme_openFontAsset(debugger.theme.miniButton.fontAsset,
                                                              miniFallback,
                                                              msize,
                                                              debugger.theme.miniButton.fontStyle);
    // Text fonts default to button font size if not explicitly set
    int baseText = debugger.theme.text.fontSize > 0 ? debugger.theme.text.fontSize : baseButton;
    int tsize = e9ui_theme_scaledSize(baseText);
    if (debugger.theme.text.source) {
        TTF_CloseFont(debugger.theme.text.source);
        debugger.theme.text.source = NULL;
    }
    if (debugger.theme.text.console) {
        TTF_CloseFont(debugger.theme.text.console);
        debugger.theme.text.console = NULL;
    }
    if (debugger.theme.text.prompt) {
        TTF_CloseFont(debugger.theme.text.prompt);
        debugger.theme.text.prompt = NULL;
    }
    debugger.theme.text.source = e9ui_theme_openFontAsset(debugger.theme.text.fontAsset,
                                                          E9UI_THEME_TEXT_FONT_ASSET,
                                                          tsize,
                                                          debugger.theme.text.fontStyle);
    debugger.theme.text.console = e9ui_theme_openFontAsset(debugger.theme.text.fontAsset,
                                                           E9UI_THEME_TEXT_FONT_ASSET,
                                                           tsize,
                                                           debugger.theme.text.fontStyle);
    debugger.theme.text.prompt = e9ui_theme_openFontAsset(debugger.theme.text.fontAsset,
                                                         E9UI_THEME_TEXT_FONT_ASSET,
                                                         tsize,
                                                         debugger.theme.text.fontStyle);
}

void
e9ui_theme_unloadFonts(void)
{
    if (debugger.theme.button.font) {
        TTF_CloseFont(debugger.theme.button.font);
        debugger.theme.button.font = NULL;
    }
    if (debugger.theme.miniButton.font) {
        TTF_CloseFont(debugger.theme.miniButton.font);
        debugger.theme.miniButton.font = NULL;
    }
    if (debugger.theme.text.source) {
        TTF_CloseFont(debugger.theme.text.source);
        debugger.theme.text.source = NULL;
    }
    if (debugger.theme.text.console) {
        TTF_CloseFont(debugger.theme.text.console);
        debugger.theme.text.console = NULL;
    }
    if (debugger.theme.text.prompt) {
        TTF_CloseFont(debugger.theme.text.prompt);
        debugger.theme.text.prompt = NULL;
    }
}

void
e9ui_theme_reloadFonts(void)
{
    e9ui_theme_unloadFonts();
    e9ui_theme_loadFonts();
    e9ui_text_cache_clear();
}

void
e9ui_theme_ctor(void)
{
    // Theme defaults
    debugger.theme.button.mask = 0;
    debugger.theme.button.highlight = E9UI_THEME_BUTTON_HIGHLIGHT_COLOR;
    debugger.theme.button.background = E9UI_THEME_BUTTON_BACKGROUND_COLOR;
    debugger.theme.button.pressedBackground = E9UI_THEME_BUTTON_PRESSED_COLOR;
    debugger.theme.button.shadow = E9UI_THEME_BUTTON_SHADOW_COLOR;
    debugger.theme.button.text = E9UI_THEME_BUTTON_TEXT_COLOR;
    debugger.theme.button.borderRadius = E9UI_THEME_BUTTON_BORDER_RADIUS;
    debugger.theme.button.fontSize = E9UI_THEME_BUTTON_FONT_SIZE;
    debugger.theme.button.font = NULL;
    debugger.theme.button.padding = E9UI_THEME_BUTTON_PADDING;
    debugger.theme.button.fontAsset = E9UI_THEME_BUTTON_FONT_ASSET;
    debugger.theme.button.fontStyle = E9UI_THEME_BUTTON_FONT_STYLE;
    debugger.theme.miniButton.mask = 0;
    debugger.theme.miniButton.highlight = debugger.theme.button.highlight;
    debugger.theme.miniButton.background = debugger.theme.button.background;
    debugger.theme.miniButton.pressedBackground = debugger.theme.button.pressedBackground;
    debugger.theme.miniButton.shadow = debugger.theme.button.shadow;
    debugger.theme.miniButton.text = debugger.theme.button.text;
    debugger.theme.miniButton.borderRadius = debugger.theme.button.borderRadius;
    debugger.theme.miniButton.fontSize = E9UI_THEME_MINI_BUTTON_FONT_SIZE;
    debugger.theme.miniButton.padding = E9UI_THEME_MINI_BUTTON_PADDING;
    debugger.theme.miniButton.font = NULL;
    debugger.theme.miniButton.fontAsset = E9UI_THEME_MINI_BUTTON_FONT_ASSET;
    debugger.theme.miniButton.fontStyle = E9UI_THEME_MINI_BUTTON_FONT_STYLE;
    debugger.theme.titlebar.background = E9UI_THEME_TITLEBAR_BACKGROUND;
    debugger.theme.titlebar.text = E9UI_THEME_TITLEBAR_TEXT;
    debugger.theme.text.fontSize = E9UI_THEME_TEXT_FONT_SIZE;
    debugger.theme.text.fontAsset = E9UI_THEME_TEXT_FONT_ASSET;
    debugger.theme.text.fontStyle = E9UI_THEME_TEXT_FONT_STYLE;
    debugger.theme.text.source = NULL;
    debugger.theme.text.console = NULL;
    debugger.theme.text.prompt = NULL;
    debugger.theme.checkbox.margin = E9UI_THEME_CHECKBOX_MARGIN;
    debugger.theme.checkbox.textGap = E9UI_THEME_CHECKBOX_TEXT_GAP;
    debugger.theme.disabled.borderScale = E9UI_THEME_DISABLED_BORDER_SCALE;
    debugger.theme.disabled.fillScale = E9UI_THEME_DISABLED_FILL_SCALE;
    debugger.theme.disabled.textScale = E9UI_THEME_DISABLED_TEXT_SCALE;
    // UI layout defaults
    debugger.layout.splitSrcConsole = E9UI_LAYOUT_SPLIT_SRC_CONSOLE;
    debugger.layout.splitUpper = E9UI_LAYOUT_SPLIT_UPPER;
    debugger.layout.splitRight = E9UI_LAYOUT_SPLIT_RIGHT;
    debugger.layout.splitLr = E9UI_LAYOUT_SPLIT_LR;
    debugger.layout.winX = E9UI_LAYOUT_WIN_X;
    debugger.layout.winY = E9UI_LAYOUT_WIN_Y;
    debugger.layout.winW = E9UI_LAYOUT_WIN_W;
    debugger.layout.winH = E9UI_LAYOUT_WIN_H;    
}  
