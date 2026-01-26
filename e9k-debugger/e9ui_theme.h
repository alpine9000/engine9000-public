/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdint.h>
#include <SDL.h>
#include <SDL_ttf.h>

typedef struct e9k_theme_button {
    uint32_t  mask;
    SDL_Color highlight;
    SDL_Color background;
    SDL_Color pressedBackground;
    SDL_Color shadow;
    SDL_Color text;
    int       borderRadius;
    int       fontSize;
    int       padding;
    const char *fontAsset;
    int       fontStyle;
    TTF_Font *font;
} e9k_theme_button_t;

#define E9K_THEME_BUTTON_MASK_HIGHLIGHT        (1u << 0)
#define E9K_THEME_BUTTON_MASK_BACKGROUND       (1u << 1)
#define E9K_THEME_BUTTON_MASK_PRESSED          (1u << 2)
#define E9K_THEME_BUTTON_MASK_SHADOW           (1u << 3)
#define E9K_THEME_BUTTON_MASK_TEXT             (1u << 4)
#define E9K_THEME_BUTTON_MASK_RADIUS           (1u << 5)
#define E9K_THEME_BUTTON_MASK_FONT_SIZE        (1u << 6)
#define E9K_THEME_BUTTON_MASK_PADDING          (1u << 7)
#define E9K_THEME_BUTTON_MASK_FONT_ASSET       (1u << 8)
#define E9K_THEME_BUTTON_MASK_FONT_STYLE       (1u << 9)
#define E9K_THEME_BUTTON_MASK_FONT             (1u << 10)
#define E9K_THEME_BUTTON_MASK_ALL              (0x7FFu)

typedef struct e9k_theme_text {
    int       fontSize;
    const char *fontAsset;
    int       fontStyle;
    TTF_Font *source;
    TTF_Font *console;
    TTF_Font *prompt;
} e9k_theme_text_t;

typedef struct e9k_theme_titlebar {
    SDL_Color background;
    SDL_Color text;
} e9k_theme_titlebar_t;

typedef struct e9k_theme_checkbox {
    int margin;
    int textGap;
} e9k_theme_checkbox_t;

typedef struct e9k_theme_disabled {
    float borderScale;
    float fillScale;
    float textScale;
} e9k_theme_disabled_t;


void
e9ui_theme_loadFonts(void);

void
e9ui_theme_unloadFonts(void);

void
e9ui_theme_reloadFonts(void);

void
e9ui_theme_ctor(void);

const e9k_theme_button_t *
e9ui_theme_button_preset_red(void);

const e9k_theme_button_t *
e9ui_theme_button_preset_green(void);

const e9k_theme_button_t *
e9ui_theme_button_preset_profile_active(void);



