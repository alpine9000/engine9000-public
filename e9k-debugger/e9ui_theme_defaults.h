/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <SDL.h>
#include <SDL_ttf.h>

// Button defaults ------------------------------------------------------------

// Highlight color used for hover/active borders around primary buttons.
#define E9UI_THEME_BUTTON_HIGHLIGHT_COLOR (SDL_Color){0x7B,0x7C,0x7C,0xFF}
// Background fill color for buttons in their normal state.
#define E9UI_THEME_BUTTON_BACKGROUND_COLOR (SDL_Color){0x5A,0x5B,0x5C,0xFF}
// Background fill color when a button is pressed or held down.
#define E9UI_THEME_BUTTON_PRESSED_COLOR (SDL_Color){0x3A,0x3A,0x3C,0xFF}
// Shadow color drawn under buttons to create depth.
#define E9UI_THEME_BUTTON_SHADOW_COLOR (SDL_Color){0x1C,0x1D,0x1D,0xFF}
// Text color used for button labels.
#define E9UI_THEME_BUTTON_TEXT_COLOR (SDL_Color){0xE6,0xE7,0xE7,0xFF}
// Corner radius in pixels for button backgrounds (rounded corners).
#define E9UI_THEME_BUTTON_BORDER_RADIUS 6
// Base logical font size used when initializing button fonts.
#define E9UI_THEME_BUTTON_FONT_SIZE 16
// Padding (in logical pixels) applied on all sides of primary buttons.
#define E9UI_THEME_BUTTON_PADDING 4
// Asset path for button fonts (relative to assets/).
#define E9UI_THEME_BUTTON_FONT_ASSET "assets/RobotoMono-SemiBold.ttf"
// SDL_ttf style flags applied to the button font.
#define E9UI_THEME_BUTTON_FONT_STYLE TTF_STYLE_NORMAL

// Mini button defaults -------------------------------------------------------

// Mini buttons share the main button palette but default to a smaller font.
#define E9UI_THEME_MINI_BUTTON_FONT_SIZE 14
// Padding (logical pixels) applied on mini buttons.
#define E9UI_THEME_MINI_BUTTON_PADDING 0
// Asset path for mini button fonts; defaults to the button font asset.
#define E9UI_THEME_MINI_BUTTON_FONT_ASSET E9UI_THEME_BUTTON_FONT_ASSET
// SDL_ttf style flags for mini button fonts.
#define E9UI_THEME_MINI_BUTTON_FONT_STYLE E9UI_THEME_BUTTON_FONT_STYLE

// Titlebar defaults ----------------------------------------------------------

// Background color for window titlebars and titled component headers.
#define E9UI_THEME_TITLEBAR_BACKGROUND (SDL_Color){0x22,0x27,0x39,0xFF}
// Text color used to render titlebar labels.
#define E9UI_THEME_TITLEBAR_TEXT (SDL_Color){0xFF,0xFF,0xFF,0xFF}

// Text defaults --------------------------------------------------------------

// Default font size for source/console/prompt text. Falls back to button size for consistency.
#define E9UI_THEME_TEXT_FONT_SIZE E9UI_THEME_BUTTON_FONT_SIZE
// Asset path for console/source/prompt fonts.
#define E9UI_THEME_TEXT_FONT_ASSET E9UI_THEME_BUTTON_FONT_ASSET
// SDL_ttf style applied to the text fonts.
#define E9UI_THEME_TEXT_FONT_STYLE E9UI_THEME_BUTTON_FONT_STYLE

// Checkbox defaults ----------------------------------------------------------

// Margin (logical pixels) inserted between the checkbox square and its label/spacing.
#define E9UI_THEME_CHECKBOX_MARGIN 2
#define E9UI_THEME_CHECKBOX_TEXT_GAP 6

// Disabled state defaults ----------------------------------------------------

// Scale factor applied to button borders/highlights when disabled.
#define E9UI_THEME_DISABLED_BORDER_SCALE 0.6f
// Scale factor applied to button fills when disabled.
#define E9UI_THEME_DISABLED_FILL_SCALE 0.55f
// Scale factor applied to button text when disabled.
#define E9UI_THEME_DISABLED_TEXT_SCALE 0.7f

// Layout defaults ------------------------------------------------------------

// Split ratio for source vs. console (space allocated to source panes).
#define E9UI_LAYOUT_SPLIT_SRC_CONSOLE 0.66f
// Ratio for registers vs. source/console split.
#define E9UI_LAYOUT_SPLIT_UPPER 0.20f
// Ratio for stack vs. memory/breakpoints stack (right column).
#define E9UI_LAYOUT_SPLIT_RIGHT 0.50f
// Ratio for left column vs. right column (source/console vs. stack/memory/etc.).
#define E9UI_LAYOUT_SPLIT_LR 0.70f
// Default window position (if no saved state exists).
#define E9UI_LAYOUT_WIN_X -1
#define E9UI_LAYOUT_WIN_Y -1
// Default window size used before user resize/persistence.
#define E9UI_LAYOUT_WIN_W 1000
#define E9UI_LAYOUT_WIN_H 700


