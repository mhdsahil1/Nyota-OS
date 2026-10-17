/* =============================================================================
 * Nyota OS — Native GUI Toolkit Theme & Design Tokens
 * Harmonious, premium dark palette, widget metrics, and standard styling.
 * =========================================================================== */

#ifndef NYOTA_LIBGUI_THEME_H
#define NYOTA_LIBGUI_THEME_H

#include "types.h"

/* 32-bit ARGB Color Tokens */
#define COLOR_DESKTOP_BG        0xFF181825  /* Deep space navy */
#define COLOR_PANEL_BG          0xFF11111B  /* Taskbar background */
#define COLOR_PANEL_BORDER      0xFF313244
#define COLOR_WINDOW_BG         0xFF1E1E2E  /* Base window body */
#define COLOR_WINDOW_BORDER     0xFF45475A
#define COLOR_TITLEBAR_ACTIVE   0xFF313244  /* Focused title bar */
#define COLOR_TITLEBAR_INACTIVE 0xFF181825  /* Unfocused title bar */
#define COLOR_TITLE_ACTIVE      0xFFCDD6F4  /* Bright white-lavender */
#define COLOR_TITLE_INACTIVE    0xFF6C7086  /* Muted grey */

/* UI Accent Tokens */
#define COLOR_ACCENT_PRIMARY    0xFF89B4FA  /* Nyota Cyan/Blue */
#define COLOR_ACCENT_HOVER      0xFFB4BEFE
#define COLOR_ACCENT_PURPLE     0xFFCBA6F7
#define COLOR_SUCCESS           0xFFA6E3A1  /* Vibrant Green */
#define COLOR_WARNING           0xFFF9E2AF  /* Amber */
#define COLOR_DANGER            0xFFF38BA8  /* Coral Red */

/* Widget Styling */
#define COLOR_WIDGET_BG         0xFF24273A
#define COLOR_WIDGET_BORDER     0xFF363A4F
#define COLOR_BUTTON_NORMAL     0xFF313244
#define COLOR_BUTTON_HOVER      0xFF45475A
#define COLOR_BUTTON_PRESSED    0xFF585B70
#define COLOR_TEXT_PRIMARY      0xFFCDD6F4
#define COLOR_TEXT_MUTED        0xFFA6ADC8
#define COLOR_TEXT_DIM          0xFF6C7086
#define COLOR_TEXTBOX_BG        0xFF181825
#define COLOR_TEXTBOX_BORDER    0xFF585B70
#define COLOR_SELECTION         0xFF585B70
#define COLOR_PROGRESS_BG       0xFF181825
#define COLOR_PROGRESS_BAR      0xFF89B4FA

/* Window Controls */
#define COLOR_BTN_CLOSE         0xFFF38BA8
#define COLOR_BTN_MAX           0xFFA6E3A1
#define COLOR_BTN_MIN           0xFFF9E2AF

/* Metrics */
#define GUI_PANEL_HEIGHT        32
#define GUI_TITLEBAR_HEIGHT     26
#define GUI_BORDER_WIDTH        2
#define GUI_FONT_WIDTH          8
#define GUI_FONT_HEIGHT         16
#define GUI_PADDING             8

#endif /* NYOTA_LIBGUI_THEME_H */
