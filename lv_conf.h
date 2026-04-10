#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ── Color ────────────────────────────────────────────────────── */
#define LV_COLOR_DEPTH 16

/* ── Memory ───────────────────────────────────────────────────── */
#define LV_MEM_SIZE (48U * 1024U)
#define LV_MEM_ADR 0
#define LV_MEM_CUSTOM 0

/* ── HAL ──────────────────────────────────────────────────────── */
#define LV_DEF_REFR_PERIOD 33      /* ~30 fps */
#define LV_DPI_DEF 130
#define LV_USE_OS LV_OS_NONE

/* ── Drawing ──────────────────────────────────────────────────── */
#define LV_USE_DRAW_SW 1
#define LV_USE_DRAW_ARM2D 0
#define LV_USE_DRAW_SDL 0
#define LV_USE_DRAW_VG_LITE 0

/* ── Logging ──────────────────────────────────────────────────── */
#define LV_USE_LOG 0

/* ── Asserts ──────────────────────────────────────────────────── */
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/* ── Fonts ────────────────────────────────────────────────────── */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 1

#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* ── Text ─────────────────────────────────────────────────────── */
#define LV_TXT_ENC LV_TXT_ENC_UTF8

/* ── Widgets — enable only what we use ────────────────────────── */
#define LV_USE_ARC          1
#define LV_USE_BAR          1
#define LV_USE_BUTTON       0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR     0
#define LV_USE_CANVAS       1
#define LV_USE_CHART        0
#define LV_USE_CHECKBOX     0
#define LV_USE_DROPDOWN     0
#define LV_USE_IMAGE        1
#define LV_USE_IMAGEBUTTON  0
#define LV_USE_KEYBOARD     0
#define LV_USE_LABEL        1
#define LV_USE_LED          0
#define LV_USE_LINE         1
#define LV_USE_LIST         0
#define LV_USE_MENU         0
#define LV_USE_MSGBOX       0
#define LV_USE_ROLLER       0
#define LV_USE_SCALE        0
#define LV_USE_SLIDER       0
#define LV_USE_SPAN         0
#define LV_USE_SPINBOX      0
#define LV_USE_SPINNER      1
#define LV_USE_SWITCH       0
#define LV_USE_TABLE        0
#define LV_USE_TABVIEW      0
#define LV_USE_TEXTAREA     0
#define LV_USE_TILEVIEW     0
#define LV_USE_WIN          0

/* ── Layouts ──────────────────────────────────────────────────── */
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/* ── Themes ───────────────────────────────────────────────────── */
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE  0
#define LV_USE_THEME_MONO    0

/* ── File-system / image decoders — none needed ───────────────── */
#define LV_USE_FS_STDIO  0
#define LV_USE_FS_POSIX  0
#define LV_USE_FS_WIN32  0
#define LV_USE_FS_FATFS  0
#define LV_USE_FS_MEMFS  0
#define LV_USE_LODEPNG   0
#define LV_USE_BMP       0
#define LV_USE_GIF       0
#define LV_USE_QRCODE    0
#define LV_USE_BARCODE   0
#define LV_USE_FREETYPE  0
#define LV_USE_TINY_TTF  0

/* ── Misc ─────────────────────────────────────────────────────── */
#define LV_USE_ANIM    1
#define LV_BUILD_EXAMPLES 0

#endif /* LV_CONF_H */
