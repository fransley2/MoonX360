#pragma once
/* MoonX360 Unified Native UI/UX Design System tokens.
 * Single source of truth for semantic colors, typography, spacing, radii,
 * dialog geometry, controller atlas dimensions, and feedback components. */
#include <stdint.h>
#include "canvas.h"

/* ---- Semantic Color Palette (Xbox Series X|S Dark Console) ---- */
#define DS_BG_BASE        HEX(0x0D1114)
#define DS_BG_TOP         HEX(0x161D22)
#define DS_BG_SURFACE     HEX(0x1B2126)
#define DS_BG_ELEVATED    HEX(0x222A30)
#define DS_BG_PILL        ARGB(155, 12, 16, 20)
#define DS_BG_FOOTER      ARGB(226, 13, 17, 20)
#define DS_BG_SCRIM       ARGB(155, 0, 0, 0)

#define DS_BORDER_SUBTLE  HEX(0x364148)
#define DS_BORDER_STRONG  HEX(0x52616B)

#define DS_TEXT_PRIMARY   HEX(0xF4F7F5)
#define DS_TEXT_SECONDARY HEX(0xB8C3BE)
#define DS_TEXT_MUTED     HEX(0x889690)

#define DS_STATUS_OK      HEX(0x72C840)
#define DS_STATUS_WARN    HEX(0xF5B94A)
#define DS_STATUS_ERROR   HEX(0xF0605D)

#define DS_BTN_A          HEX(0x5FAE27)
#define DS_BTN_B          HEX(0xCE4038)
#define DS_BTN_X          HEX(0x3678C0)
#define DS_BTN_Y          HEX(0xD6AF25)
#define DS_BTN_BUMPER     HEX(0xCFD6DA)
#define DS_BTN_TRIGGER    HEX(0x9AA6AE)
#define DS_BTN_SYSTEM     HEX(0x303A42)

/* ---- Typography Scale (logical 720p px) ---- */
#define DS_TYPE_DISPLAY   56
#define DS_TYPE_TITLE     30
#define DS_TYPE_SECTION   24
#define DS_TYPE_BODY      22
#define DS_TYPE_SUB       18
#define DS_TYPE_CAPTION   15
#define DS_TYPE_BADGE     12

/* ---- Spacing & Layout Scale ---- */
#define DS_SPACE_XS       4
#define DS_SPACE_SM       8
#define DS_SPACE_MD       16
#define DS_SPACE_LG       24
#define DS_SPACE_XL       32
#define DS_SAFE_X         64

/* ---- Radii & Strokes ---- */
#define DS_RADIUS_SM      4
#define DS_RADIUS_MD      6
#define DS_RADIUS_LG      8
#define DS_RADIUS_PILL    12
#define DS_FOCUS_BORDER   3

/* ---- Standard Dialog Widths (auto-clamped to W - 2*DS_SAFE_X for 4:3) ---- */
#define DS_DIALOG_SM      640
#define DS_DIALOG_MD      720
#define DS_DIALOG_LG      780

/* ---- Controller Icon Atlas (single persistent 64 KB RGBA texture) ---- */
#define UI_BUTTON_ATLAS_W 512
#define UI_BUTTON_ATLAS_H 32

/* ---- Toast & Context Menu Constants ---- */
enum ui_toast_kind { UI_TOAST_INFO, UI_TOAST_OK, UI_TOAST_WARN, UI_TOAST_ERROR };
#define UI_TOAST_DEFAULT_MS 2600u
#define UI_TOAST_FADE_MS    180u
#define UI_CONTEXT_MAX_VISIBLE 7
