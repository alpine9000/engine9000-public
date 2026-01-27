/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

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
    float scale = e9ui->ctx.dpiScale;
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
    if (e9ui->theme.button.font) {
        TTF_CloseFont(e9ui->theme.button.font);
        e9ui->theme.button.font = NULL;
    }
    int baseButton = e9ui->theme.button.fontSize > 0 ? e9ui->theme.button.fontSize : 18;
    int bsize = e9ui_theme_scaledSize(baseButton);
    e9ui->theme.button.font = e9ui_theme_openFontAsset(e9ui->theme.button.fontAsset,
                                                          E9UI_THEME_BUTTON_FONT_ASSET,
                                                          bsize,
                                                          e9ui->theme.button.fontStyle);
    // Mini button font
    if (e9ui->theme.miniButton.font) {
        TTF_CloseFont(e9ui->theme.miniButton.font);
        e9ui->theme.miniButton.font = NULL;
    }
    int baseMini = e9ui->theme.miniButton.fontSize > 0 ? e9ui->theme.miniButton.fontSize : baseButton;
    int msize = e9ui_theme_scaledSize(baseMini);
    const char *miniFallback = e9ui->theme.button.fontAsset ? e9ui->theme.button.fontAsset : E9UI_THEME_MINI_BUTTON_FONT_ASSET;
    e9ui->theme.miniButton.font = e9ui_theme_openFontAsset(e9ui->theme.miniButton.fontAsset,
                                                              miniFallback,
                                                              msize,
                                                              e9ui->theme.miniButton.fontStyle);
    // Text fonts default to button font size if not explicitly set
    int baseText = e9ui->theme.text.fontSize > 0 ? e9ui->theme.text.fontSize : baseButton;
    int tsize = e9ui_theme_scaledSize(baseText);
    if (e9ui->theme.text.source) {
        TTF_CloseFont(e9ui->theme.text.source);
        e9ui->theme.text.source = NULL;
    }
    if (e9ui->theme.text.console) {
        TTF_CloseFont(e9ui->theme.text.console);
        e9ui->theme.text.console = NULL;
    }
    if (e9ui->theme.text.prompt) {
        TTF_CloseFont(e9ui->theme.text.prompt);
        e9ui->theme.text.prompt = NULL;
    }
    e9ui->theme.text.source = e9ui_theme_openFontAsset(e9ui->theme.text.fontAsset,
                                                          E9UI_THEME_TEXT_FONT_ASSET,
                                                          tsize,
                                                          e9ui->theme.text.fontStyle);
    e9ui->theme.text.console = e9ui_theme_openFontAsset(e9ui->theme.text.fontAsset,
                                                           E9UI_THEME_TEXT_FONT_ASSET,
                                                           tsize,
                                                           e9ui->theme.text.fontStyle);
    e9ui->theme.text.prompt = e9ui_theme_openFontAsset(e9ui->theme.text.fontAsset,
                                                         E9UI_THEME_TEXT_FONT_ASSET,
                                                         tsize,
                                                         e9ui->theme.text.fontStyle);
}

void
e9ui_theme_unloadFonts(void)
{
    if (e9ui->theme.button.font) {
        TTF_CloseFont(e9ui->theme.button.font);
        e9ui->theme.button.font = NULL;
    }
    if (e9ui->theme.miniButton.font) {
        TTF_CloseFont(e9ui->theme.miniButton.font);
        e9ui->theme.miniButton.font = NULL;
    }
    if (e9ui->theme.text.source) {
        TTF_CloseFont(e9ui->theme.text.source);
        e9ui->theme.text.source = NULL;
    }
    if (e9ui->theme.text.console) {
        TTF_CloseFont(e9ui->theme.text.console);
        e9ui->theme.text.console = NULL;
    }
    if (e9ui->theme.text.prompt) {
        TTF_CloseFont(e9ui->theme.text.prompt);
        e9ui->theme.text.prompt = NULL;
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
    e9ui->theme.button.mask = 0;
    e9ui->theme.button.highlight = E9UI_THEME_BUTTON_HIGHLIGHT_COLOR;
    e9ui->theme.button.background = E9UI_THEME_BUTTON_BACKGROUND_COLOR;
    e9ui->theme.button.pressedBackground = E9UI_THEME_BUTTON_PRESSED_COLOR;
    e9ui->theme.button.shadow = E9UI_THEME_BUTTON_SHADOW_COLOR;
    e9ui->theme.button.text = E9UI_THEME_BUTTON_TEXT_COLOR;
    e9ui->theme.button.borderRadius = E9UI_THEME_BUTTON_BORDER_RADIUS;
    e9ui->theme.button.fontSize = E9UI_THEME_BUTTON_FONT_SIZE;
    e9ui->theme.button.font = NULL;
    e9ui->theme.button.padding = E9UI_THEME_BUTTON_PADDING;
    e9ui->theme.button.fontAsset = E9UI_THEME_BUTTON_FONT_ASSET;
    e9ui->theme.button.fontStyle = E9UI_THEME_BUTTON_FONT_STYLE;
    e9ui->theme.miniButton.mask = 0;
    e9ui->theme.miniButton.highlight = e9ui->theme.button.highlight;
    e9ui->theme.miniButton.background = e9ui->theme.button.background;
    e9ui->theme.miniButton.pressedBackground = e9ui->theme.button.pressedBackground;
    e9ui->theme.miniButton.shadow = e9ui->theme.button.shadow;
    e9ui->theme.miniButton.text = e9ui->theme.button.text;
    e9ui->theme.miniButton.borderRadius = e9ui->theme.button.borderRadius;
    e9ui->theme.miniButton.fontSize = E9UI_THEME_MINI_BUTTON_FONT_SIZE;
    e9ui->theme.miniButton.padding = E9UI_THEME_MINI_BUTTON_PADDING;
    e9ui->theme.miniButton.font = NULL;
    e9ui->theme.miniButton.fontAsset = E9UI_THEME_MINI_BUTTON_FONT_ASSET;
    e9ui->theme.miniButton.fontStyle = E9UI_THEME_MINI_BUTTON_FONT_STYLE;
    e9ui->theme.titlebar.background = E9UI_THEME_TITLEBAR_BACKGROUND;
    e9ui->theme.titlebar.text = E9UI_THEME_TITLEBAR_TEXT;
    e9ui->theme.text.fontSize = E9UI_THEME_TEXT_FONT_SIZE;
    e9ui->theme.text.fontAsset = E9UI_THEME_TEXT_FONT_ASSET;
    e9ui->theme.text.fontStyle = E9UI_THEME_TEXT_FONT_STYLE;
    e9ui->theme.text.source = NULL;
    e9ui->theme.text.console = NULL;
    e9ui->theme.text.prompt = NULL;
    e9ui->theme.checkbox.margin = E9UI_THEME_CHECKBOX_MARGIN;
    e9ui->theme.checkbox.textGap = E9UI_THEME_CHECKBOX_TEXT_GAP;
    e9ui->theme.disabled.borderScale = E9UI_THEME_DISABLED_BORDER_SCALE;
    e9ui->theme.disabled.fillScale = E9UI_THEME_DISABLED_FILL_SCALE;
    e9ui->theme.disabled.textScale = E9UI_THEME_DISABLED_TEXT_SCALE;
    // UI layout defaults
    e9ui->layout.splitSrcConsole = E9UI_LAYOUT_SPLIT_SRC_CONSOLE;
    e9ui->layout.splitUpper = E9UI_LAYOUT_SPLIT_UPPER;
    e9ui->layout.splitRight = E9UI_LAYOUT_SPLIT_RIGHT;
    e9ui->layout.splitLr = E9UI_LAYOUT_SPLIT_LR;
    e9ui->layout.winX = E9UI_LAYOUT_WIN_X;
    e9ui->layout.winY = E9UI_LAYOUT_WIN_Y;
    e9ui->layout.winW = E9UI_LAYOUT_WIN_W;
    e9ui->layout.winH = E9UI_LAYOUT_WIN_H;    
}  
