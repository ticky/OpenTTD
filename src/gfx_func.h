/* $Id$ */

/** @file gfx_func.h Functions related to the gfx engine. */

/**
 * @defgroup dirty Dirty
 *
 * Handles the repaint of some part of the screen.
 *
 * Some places in the code are called functions which makes something "dirty".
 * This has nothing to do with making a Tile or Window darker or less visible.
 * This term comes from memory caching and is used to define an object must
 * be repaint. If some data of an object (like a Tile, Window, Vehicle, whatever)
 * are changed which are so extensive the object must be repaint its marked
 * as "dirty". The video driver repaint this object instead of the whole screen
 * (this is btw. also possible if needed). This is used to avoid a
 * flickering of the screen by the video driver constantly repainting it.
 *
 * This whole mechanism is controlled by an rectangle defined in #_invalid_rect. This
 * rectangle defines the area on the screen which must be repaint. If a new object
 * needs to be repainted this rectangle is extended to 'catch' the object on the
 * screen. At some point (which is normaly uninteressted for patch writers) this
 * rectangle is send to the video drivers method
 * VideoDriver::MakeDirty and it is truncated back to an empty rectangle. At some
 * later point (which is uninteressted, too) the video driver
 * repaints all these saved rectangle instead of the whole screen and drop the
 * rectangle informations. Then a new round begins by marking objects "dirty".
 *
 * @see VideoDriver::MakeDirty
 * @see _invalid_rect
 * @see _screen
 */


#ifndef GFX_FUNC_H
#define GFX_FUNC_H

#include "gfx_type.h"
#include "strings_type.h"

void GameLoop();

void CreateConsole();

extern byte _dirkeys;        ///< 1 = left, 2 = up, 4 = right, 8 = down
extern bool _fullscreen;
extern CursorVars _cursor;
extern bool _ctrl_pressed;   ///< Is Ctrl pressed?
extern bool _shift_pressed;  ///< Is Shift pressed?
extern byte _fast_forward;

extern bool _left_button_down;
extern bool _left_button_clicked;
extern bool _right_button_down;
extern bool _right_button_clicked;

extern DrawPixelInfo _screen;
extern bool _screen_disable_anim;   ///< Disable palette animation (important for 32bpp-anim blitter during giant screenshot)

extern int _pal_first_dirty;
extern int _pal_count_dirty;
extern int _num_resolutions;
extern uint16 _resolutions[32][2];
extern uint16 _cur_resolution[2];
extern Colour _cur_palette[256]; ///< Current palette. Entry 0 has to be always fully transparent!

void HandleKeypress(uint32 key);
void HandleCtrlChanged();
void HandleMouseEvents();
void CSleep(int milliseconds);
void UpdateWindows();

void DrawChatMessage();
void HandleExitGameRequest();
void GameSizeChanged();

void RedrawScreenRect(int left, int top, int right, int bottom);
void GfxScroll(int left, int top, int width, int height, int xo, int yo);

void DrawSprite(SpriteID img, PaletteID pal, int x, int y, const SubSprite *sub = NULL);

int DrawStringCentered(int x, int y, StringID str, uint16 color);
int DrawStringCenteredTruncated(int xl, int xr, int y, StringID str, uint16 color);
int DoDrawStringCentered(int x, int y, const char *str, uint16 color);

int DrawString(int x, int y, StringID str, uint16 color);
int DrawStringTruncated(int x, int y, StringID str, uint16 color, uint maxw);

int DoDrawString(const char *string, int x, int y, uint16 real_colour, bool parse_string_also_when_clipped = false);
int DoDrawStringTruncated(const char *str, int x, int y, uint16 color, uint maxw);

void DrawStringCenterUnderline(int x, int y, StringID str, uint16 color);
void DrawStringCenterUnderlineTruncated(int xl, int xr, int y, StringID str, uint16 color);

int DrawStringRightAligned(int x, int y, StringID str, uint16 color);
void DrawStringRightAlignedTruncated(int x, int y, StringID str, uint16 color, uint maxw);
void DrawStringRightAlignedUnderline(int x, int y, StringID str, uint16 color);

void DrawCharCentered(uint32 c, int x, int y, uint16 color);

void GfxFillRect(int left, int top, int right, int bottom, int color);
void GfxDrawLine(int left, int top, int right, int bottom, int color);
void DrawBox(int x, int y, int dx1, int dy1, int dx2, int dy2, int dx3, int dy3);

Dimension GetStringBoundingBox(const char *str);
uint32 FormatStringLinebreaks(char *str, int maxw);
void LoadStringWidthTable();
void DrawStringMultiCenter(int x, int y, StringID str, int maxw);
uint DrawStringMultiLine(int x, int y, StringID str, int maxw, int maxh = -1);

/**
 * Let the dirty blocks repainting by the video driver.
 *
 * @ingroup dirty
 */
void DrawDirtyBlocks();

/**
 * Set a new dirty block.
 *
 * @ingroup dirty
 */
void SetDirtyBlocks(int left, int top, int right, int bottom);

/**
 * Marks the whole screen as dirty.
 *
 * @ingroup dirty
 */
void MarkWholeScreenDirty();

void GfxInitPalettes();

bool FillDrawPixelInfo(DrawPixelInfo* n, int left, int top, int width, int height);

/* window.cpp */
void DrawOverlappedWindowForAll(int left, int top, int right, int bottom);

void SetMouseCursor(CursorID cursor, PaletteID pal);
void SetAnimatedMouseCursor(const AnimCursor *table);
void CursorTick();
bool ChangeResInGame(int w, int h);
void SortResolutions(int count);
bool ToggleFullScreen(bool fs);

/* gfx.cpp */
#define ASCII_LETTERSTART 32
extern FontSize _cur_fontsize;

byte GetCharacterWidth(FontSize size, uint32 key);
void DrawMouseCursor();
void ScreenSizeChanged();
void UndrawMouseCursor();

/**
 * Get height of a character for a given font size.
 * @param size Font size to get height of
 * @return     Height of characters in the given font (pixels)
 */
static inline byte GetCharacterHeight(FontSize size)
{
	switch (size) {
		default: NOT_REACHED();
		case FS_NORMAL: return 10;
		case FS_SMALL:  return 6;
		case FS_LARGE:  return 18;
	}
}

extern DrawPixelInfo *_cur_dpi;

/**
 * All 16 colour gradients
 * 8 colours per gradient from darkest (0) to lightest (7)
 */
extern byte _colour_gradient[16][8];

extern bool _use_dos_palette;

/**
 * Return the colour for a particular greyscale level.
 * @param level Intensity, 0 = black, 15 = white
 * @return colour
 */
#define GREY_SCALE(level) (level)

static const uint8 PC_BLACK              = GREY_SCALE(1);  ///< Black palette colour.
static const uint8 PC_DARK_GREY          = GREY_SCALE(6);  ///< Dark grey palette colour.
static const uint8 PC_GREY               = GREY_SCALE(10); ///< Grey palette colour.
static const uint8 PC_WHITE              = GREY_SCALE(15); ///< White palette colour.

static const uint8 PC_VERY_DARK_RED      = 0xB2;           ///< Almost-black red palette colour.
static const uint8 PC_DARK_RED           = 0xB4;           ///< Dark red palette colour.
static const uint8 PC_RED                = 0xB8;           ///< Red palette colour.

static const uint8 PC_VERY_DARK_BROWN    = 0x56;           ///< Almost-black brown palette colour.

static const uint8 PC_ORANGE             = 0xC2;           ///< Orange palette colour.

static const uint8 PC_YELLOW             = 0xBF;           ///< Yellow palette colour.
static const uint8 PC_LIGHT_YELLOW       = 0x44;           ///< Light yellow palette colour.
static const uint8 PC_VERY_LIGHT_YELLOW  = 0x45;           ///< Almost-white yellow palette colour.

static const uint8 PC_GREEN              = 0xD0;           ///< Green palette colour.

static const uint8 PC_DARK_BLUE          = 0x9D;           ///< Dark blue palette colour.
static const uint8 PC_LIGHT_BLUE         = 0x98;           ///< Light blue palette colour.

static const uint8 PC_ROUGH_LAND         = 0x52;           ///< Dark green palette colour for rough land.
static const uint8 PC_GRASS_LAND         = 0x54;           ///< Dark green palette colour for grass land.
static const uint8 PC_BARE_LAND          = 0x37;           ///< Brown palette colour for bare land.
static const uint8 PC_FIELDS             = 0x25;           ///< Light brown palette colour for fields.
static const uint8 PC_TREES              = 0x57;           ///< Green palette colour for trees.
static const uint8 PC_WATER              = 0xC9;           ///< Dark blue palette colour for water.
#endif /* GFX_FUNC_H */
