/* $Id$ */

/** @file screenshot.cpp The creation of screenshots! */

#include "stdafx.h"
#include "openttd.h"
#include "debug.h"
#include "fileio.h"
#include "viewport_func.h"
#include "tile_map.h"
#include "gfx_func.h"
#include "core/math_func.hpp"
#include "screenshot.h"
#include "variables.h"
#include "blitter/factory.hpp"
#include "zoom_func.h"
#include "core/alloc_func.hpp"
#include "core/endian_func.hpp"
#include "map_func.h"
#include "saveload.h"
#include "player_base.h"
#include "player_func.h"
#include "strings_func.h"
#include "gui.h"
#include "window_gui.h"
#include "window_func.h"

#include "table/strings.h"

#include "safeguards.h"

static const char * const SCREENSHOT_NAME = "screenshot"; ///< Default filename of a saved screenshot.
static const char * const HEIGHTMAP_NAME  = "heightmap";  ///< Default filename of a saved heightmap.

char _screenshot_format_name[8];
uint _num_screenshot_formats;
uint _cur_screenshot_format;
char _screenshot_name[128];
char _full_screenshot_name[MAX_PATH];

/* called by the ScreenShot proc to generate screenshot lines. */
typedef void ScreenshotCallback(void *userdata, void *buf, uint y, uint pitch, uint n);
typedef bool ScreenshotHandlerProc(const char *name, ScreenshotCallback *callb, void *userdata, uint w, uint h, int pixelformat, const Colour *palette);

struct ScreenshotFormat {
	const char *name;
	const char *extension;
	ScreenshotHandlerProc *proc;
};

#define MKCOLOUR(x)         TO_LE32X(x)

/*************************************************
 **** SCREENSHOT CODE FOR WINDOWS BITMAP (.BMP)
 *************************************************/
#if defined(_MSC_VER) || defined(__WATCOMC__)
#pragma pack(push, 1)
#endif

struct BitmapFileHeader {
	uint16 type;
	uint32 size;
	uint32 reserved;
	uint32 off_bits;
} GCC_PACK;
assert_compile(sizeof(BitmapFileHeader) == 14);

#if defined(_MSC_VER) || defined(__WATCOMC__)
#pragma pack(pop)
#endif

struct BitmapInfoHeader {
	uint32 size;
	int32 width, height;
	uint16 planes, bitcount;
	uint32 compression, sizeimage, xpels, ypels, clrused, clrimp;
};
assert_compile(sizeof(BitmapInfoHeader) == 40);

struct RgbQuad {
	byte blue, green, red, reserved;
};
assert_compile(sizeof(RgbQuad) == 4);

/* generic .BMP writer */
static bool MakeBmpImage(const char *name, ScreenshotCallback *callb, void *userdata, uint w, uint h, int pixelformat, const Colour *palette)
{
	BitmapFileHeader bfh;
	BitmapInfoHeader bih;
	RgbQuad rq[256];
	FILE *f;
	uint i, padw;
	uint n, maxlines;
	uint pal_size = 0;
	uint bpp = pixelformat / 8;

	/* only implemented for 8bit and 32bit images so far. */
	if (pixelformat != 8 && pixelformat != 32) return false;

	f = fopen(name, "wb");
	if (f == NULL) return false;

	/* each scanline must be aligned on a 32bit boundary */
	padw = Align(w, 4);

	if (pixelformat == 8) pal_size = sizeof(RgbQuad) * 256;

	/* setup the file header */
	bfh.type = TO_LE16('MB');
	bfh.size = TO_LE32(sizeof(bfh) + sizeof(bih) + pal_size + padw * h * bpp);
	bfh.reserved = 0;
	bfh.off_bits = TO_LE32(sizeof(bfh) + sizeof(bih) + pal_size);

	/* setup the info header */
	bih.size = TO_LE32(sizeof(BitmapInfoHeader));
	bih.width = TO_LE32(w);
	bih.height = TO_LE32(h);
	bih.planes = TO_LE16(1);
	bih.bitcount = TO_LE16(pixelformat);
	bih.compression = 0;
	bih.sizeimage = 0;
	bih.xpels = 0;
	bih.ypels = 0;
	bih.clrused = 0;
	bih.clrimp = 0;

	if (pixelformat == 8) {
		/* convert the palette to the windows format */
		for (i = 0; i != 256; i++) {
			rq[i].red   = palette[i].r;
			rq[i].green = palette[i].g;
			rq[i].blue  = palette[i].b;
			rq[i].reserved = 0;
		}
	}

	/* write file header and info header and palette */
	if (fwrite(&bfh, sizeof(bfh), 1, f) != 1) return false;
	if (fwrite(&bih, sizeof(bih), 1, f) != 1) return false;
	if (pixelformat == 8) if (fwrite(rq, sizeof(rq), 1, f) != 1) return false;

	/* use by default 64k temp memory */
	maxlines = Clamp(65536 / padw, 16, 128);

	/* now generate the bitmap bits */
	void *buff = MallocT<uint8>(padw * maxlines * bpp); // by default generate 128 lines at a time.
	if (buff == NULL) {
		fclose(f);
		return false;
	}
	memset(buff, 0, padw * maxlines); // zero the buffer to have the padding bytes set to 0

	/* start at the bottom, since bitmaps are stored bottom up. */
	do {
		/* determine # lines */
		n = min(h, maxlines);
		h -= n;

		/* render the pixels */
		callb(userdata, buff, h, padw, n);

		/* write each line */
		while (n)
			if (fwrite((uint8 *)buff + (--n) * padw * bpp, padw * bpp, 1, f) != 1) {
				free(buff);
				fclose(f);
				return false;
			}
	} while (h != 0);

	free(buff);
	fclose(f);

	return true;
}

//********************************************************
//*** SCREENSHOT CODE FOR PORTABLE NETWORK GRAPHICS (.PNG)
//********************************************************
#if defined(WITH_PNG)
#include <png.h>

static void PNGAPI png_my_error(png_structp png_ptr, png_const_charp message)
{
	DEBUG(misc, 0, "[libpng] error: %s - %s", message, (const char *)png_get_error_ptr(png_ptr));
	longjmp(png_jmpbuf(png_ptr), 1);
}

static void PNGAPI png_my_warning(png_structp png_ptr, png_const_charp message)
{
	DEBUG(misc, 1, "[libpng] warning: %s - %s", message, (char *)png_get_error_ptr(png_ptr));
}

static bool MakePNGImage(const char *name, ScreenshotCallback *callb, void *userdata, uint w, uint h, int pixelformat, const Colour *palette)
{
	png_color rq[256];
	FILE *f;
	uint i, y, n;
	uint maxlines;
	uint bpp = pixelformat / 8;
	png_structp png_ptr;
	png_infop info_ptr;

	/* only implemented for 8bit and 32bit images so far. */
	if (pixelformat != 8 && pixelformat != 32) return false;

	f = fopen(name, "wb");
	if (f == NULL) return false;

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, (char *)name, png_my_error, png_my_warning);

	if (png_ptr == NULL) {
		fclose(f);
		return false;
	}

	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
		fclose(f);
		return false;
	}

	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_write_struct(&png_ptr, &info_ptr);
		fclose(f);
		return false;
	}

	png_init_io(png_ptr, f);

	png_set_filter(png_ptr, 0, PNG_FILTER_NONE);

	png_set_IHDR(png_ptr, info_ptr, w, h, 8, pixelformat == 8 ? PNG_COLOR_TYPE_PALETTE : PNG_COLOR_TYPE_RGB,
		PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	if (pixelformat == 8) {
		/* convert the palette to the .PNG format. */
		for (i = 0; i != 256; i++) {
			rq[i].red   = palette[i].r;
			rq[i].green = palette[i].g;
			rq[i].blue  = palette[i].b;
		}

		png_set_PLTE(png_ptr, info_ptr, rq, 256);
	}

	png_write_info(png_ptr, info_ptr);
	png_set_flush(png_ptr, 512);

	if (pixelformat == 32) {
		png_color_8 sig_bit;

		/* Save exact color/alpha resolution */
		sig_bit.alpha = 0;
		sig_bit.blue  = 8;
		sig_bit.green = 8;
		sig_bit.red   = 8;
		sig_bit.gray  = 8;
		png_set_sBIT(png_ptr, info_ptr, &sig_bit);

#if TTD_ENDIAN == TTD_LITTLE_ENDIAN
		png_set_bgr(png_ptr);
		png_set_filler(png_ptr, 0, PNG_FILLER_AFTER);
#else
		png_set_filler(png_ptr, 0, PNG_FILLER_BEFORE);
#endif /* TTD_ENDIAN == TTD_LITTLE_ENDIAN */
	}

	/* use by default 64k temp memory */
	maxlines = Clamp(65536 / w, 16, 128);

	/* now generate the bitmap bits */
	void *buff = MallocT<uint8>(w * maxlines * bpp); // by default generate 128 lines at a time.
	if (buff == NULL) {
		png_destroy_write_struct(&png_ptr, &info_ptr);
		fclose(f);
		return false;
	}
	memset(buff, 0, w * maxlines * bpp);

	y = 0;
	do {
		/* determine # lines to write */
		n = min(h - y, maxlines);

		/* render the pixels into the buffer */
		callb(userdata, buff, y, w, n);
		y += n;

		/* write them to png */
		for (i = 0; i != n; i++)
			png_write_row(png_ptr, (png_bytep)buff + i * w * bpp);
	} while (y != h);

	png_write_end(png_ptr, info_ptr);
	png_destroy_write_struct(&png_ptr, &info_ptr);

	free(buff);
	fclose(f);
	return true;
}
#endif /* WITH_PNG */


//************************************************
//*** SCREENSHOT CODE FOR ZSOFT PAINTBRUSH (.PCX)
//************************************************

struct PcxHeader {
	byte manufacturer;
	byte version;
	byte rle;
	byte bpp;
	uint32 unused;
	uint16 xmax, ymax;
	uint16 hdpi, vdpi;
	byte pal_small[16 * 3];
	byte reserved;
	byte planes;
	uint16 pitch;
	uint16 cpal;
	uint16 width;
	uint16 height;
	byte filler[54];
};
assert_compile(sizeof(PcxHeader) == 128);

static bool MakePCXImage(const char *name, ScreenshotCallback *callb, void *userdata, uint w, uint h, int pixelformat, const Colour *palette)
{
	FILE *f;
	uint maxlines;
	uint y;
	PcxHeader pcx;
	bool success;

	if (pixelformat == 32) {
		DEBUG(misc, 0, "Can't convert a 32bpp screenshot to PCX format. Please pick an other format.");
		return false;
	}
	if (pixelformat != 8 || w == 0)
		return false;

	f = fopen(name, "wb");
	if (f == NULL) return false;

	memset(&pcx, 0, sizeof(pcx));

	/* setup pcx header */
	pcx.manufacturer = 10;
	pcx.version = 5;
	pcx.rle = 1;
	pcx.bpp = 8;
	pcx.xmax = TO_LE16(w - 1);
	pcx.ymax = TO_LE16(h - 1);
	pcx.hdpi = TO_LE16(320);
	pcx.vdpi = TO_LE16(320);

	pcx.planes = 1;
	pcx.cpal = TO_LE16(1);
	pcx.width = pcx.pitch = TO_LE16(w);
	pcx.height = TO_LE16(h);

	/* write pcx header */
	if (fwrite(&pcx, sizeof(pcx), 1, f) != 1) {
		fclose(f);
		return false;
	}

	/* use by default 64k temp memory */
	maxlines = Clamp(65536 / w, 16, 128);

	/* now generate the bitmap bits */
	uint8 *buff = MallocT<uint8>(w * maxlines); // by default generate 128 lines at a time.
	if (buff == NULL) {
		fclose(f);
		return false;
	}
	memset(buff, 0, w * maxlines); // zero the buffer to have the padding bytes set to 0

	y = 0;
	do {
		/* determine # lines to write */
		uint n = min(h - y, maxlines);
		uint i;

		/* render the pixels into the buffer */
		callb(userdata, buff, y, w, n);
		y += n;

		/* write them to pcx */
		for (i = 0; i != n; i++) {
			const uint8 *bufp = buff + i * w;
			byte runchar = bufp[0];
			uint runcount = 1;
			uint j;

			/* for each pixel... */
			for (j = 1; j < w; j++) {
				uint8 ch = bufp[j];

				if (ch != runchar || runcount >= 0x3f) {
					if (runcount > 1 || (runchar & 0xC0) == 0xC0)
						if (fputc(0xC0 | runcount, f) == EOF) {
							free(buff);
							fclose(f);
							return false;
						}
					if (fputc(runchar, f) == EOF) {
						free(buff);
						fclose(f);
						return false;
					}
					runcount = 0;
					runchar = ch;
				}
				runcount++;
			}

			/* write remaining bytes.. */
			if (runcount > 1 || (runchar & 0xC0) == 0xC0)
				if (fputc(0xC0 | runcount, f) == EOF) {
					free(buff);
					fclose(f);
					return false;
				}
			if (fputc(runchar, f) == EOF) {
				free(buff);
				fclose(f);
				return false;
			}
		}
	} while (y != h);

	free(buff);

	/* write 8-bit color palette */
	if (fputc(12, f) == EOF) {
		fclose(f);
		return false;
	}

	/* Palette is word-aligned, copy it to a temporary byte array */
	byte tmp[256 * 3];

	for (uint i = 0; i < 256; i++) {
		tmp[i * 3 + 0] = palette[i].r;
		tmp[i * 3 + 1] = palette[i].g;
		tmp[i * 3 + 2] = palette[i].b;
	}
	success = fwrite(tmp, sizeof(tmp), 1, f) == 1;

	fclose(f);

	return success;
}

//************************************************
//*** GENERIC SCREENSHOT CODE
//************************************************

static const ScreenshotFormat _screenshot_formats[] = {
#if defined(WITH_PNG)
	{"PNG", "png", &MakePNGImage},
#endif
	{"BMP", "bmp", &MakeBmpImage},
	{"PCX", "pcx", &MakePCXImage},
};

/** Get filename extension of current screenshot file format. */
const char *GetCurrentScreenshotExtension()
{
	return _screenshot_formats[_cur_screenshot_format].extension;
}

/** Initialize screenshot format information on startup, with #_screenshot_format_name filled from the loadsave code. */
void InitializeScreenshotFormats()
{
	int i, j;
	for (i = 0, j = 0; i != lengthof(_screenshot_formats); i++)
		if (!strcmp(_screenshot_format_name, _screenshot_formats[i].extension)) {
			j = i;
			break;
		}
	_cur_screenshot_format = j;
	_num_screenshot_formats = lengthof(_screenshot_formats);
}

const char *GetScreenshotFormatDesc(int i)
{
	return _screenshot_formats[i].name;
}

void SetScreenshotFormat(uint i)
{
	_cur_screenshot_format = i;
	strcpy(_screenshot_format_name, _screenshot_formats[i].extension);
}

/* screenshot generator that dumps the current video buffer */
static void CurrentScreenCallback(void *userdata, void *buf, uint y, uint pitch, uint n)
{
	Window *w;
	uint x = 0;
	uint width = _screen.width;

	if (userdata != NULL) {
		w     = (Window *)userdata;
		x     = x + w->left;
		y     = y + w->top;
		width = w->width;
	}

	Blitter *blitter = BlitterFactoryBase::GetCurrentBlitter();
	void *src = blitter->MoveTo(_screen.dst_ptr, x, y);
	blitter->CopyImageToBuffer(src, buf, width, n, pitch);
}

/** generate a large piece of the world
 * @param userdata Viewport area to draw
 * @param buf Videobuffer with same bitdepth as current blitter
 * @param y First line to render
 * @param pitch Pitch of the videobuffer
 * @param n Number of lines to render
 */
static void LargeWorldCallback(void *userdata, void *buf, uint y, uint pitch, uint n)
{
	ViewPort *vp = (ViewPort *)userdata;
	DrawPixelInfo dpi, *old_dpi;
	int wx, left;

	/* We are no longer rendering to the screen */
	DrawPixelInfo old_screen = _screen;
	bool old_disable_anim = _screen_disable_anim;

	_screen.dst_ptr = buf;
	_screen.width = pitch;
	_screen.height = n;
	_screen.pitch = pitch;
	_screen_disable_anim = true;

	old_dpi = _cur_dpi;
	_cur_dpi = &dpi;

	dpi.dst_ptr = buf;
	dpi.height = n;
	dpi.width = vp->width;
	dpi.pitch = pitch;
	dpi.zoom = ZOOM_LVL_WORLD_SCREENSHOT;
	dpi.left = 0;
	dpi.top = y;

	/* Render viewport in blocks of 1600 pixels width */
	left = 0;
	while (vp->width - left != 0) {
		wx = min(vp->width - left, 1600);
		left += wx;

		ViewportDoDraw(vp,
			ScaleByZoom(left - wx - vp->left, vp->zoom) + vp->virtual_left,
			ScaleByZoom(y - vp->top, vp->zoom) + vp->virtual_top,
			ScaleByZoom(left - vp->left, vp->zoom) + vp->virtual_left,
			ScaleByZoom((y + n) - vp->top, vp->zoom) + vp->virtual_top
		);
	}

	_cur_dpi = old_dpi;

	/* Switch back to rendering to the screen */
	_screen = old_screen;
	_screen_disable_anim = old_disable_anim;
}

/**
 * Construct a pathname for a screenshot file.
 * @param default_fn Default filename.
 * @param ext        Extension to use.
 * @return Pathname for a screenshot file.
 */
static const char *MakeScreenshotName(const char *default_fn, const char *ext)
{
	bool generate = StrEmpty(_screenshot_name);

	if (generate) {
		if (_game_mode == GM_EDITOR || _game_mode == GM_MENU || _local_player == PLAYER_SPECTATOR) {
			strecpy(_screenshot_name, default_fn, lastof(_screenshot_name));
		} else {
			GenerateDefaultSaveName(_screenshot_name, lastof(_screenshot_name));
		}
	}

	/* Add extension to screenshot file */
	size_t len = strlen(_screenshot_name);
	snprintf(&_screenshot_name[len], lengthof(_screenshot_name) - len, ".%s", ext);

	for (uint serial = 1;; serial++) {
		if (snprintf(_full_screenshot_name, lengthof(_full_screenshot_name), "%s%s", _personal_dir, _screenshot_name) >= (int)lengthof(_full_screenshot_name)) {
			/* We need more characters than MAX_PATH -> end with error */
			_full_screenshot_name[0] = '\0';
			break;
		}
		if (!generate) break; // allow overwriting of non-automatic filenames
		if (!FileExists(_full_screenshot_name)) break;
		/* If file exists try another one with same name, but just with a higher index */
		snprintf(&_screenshot_name[len], lengthof(_screenshot_name) - len, "#%u.%s", serial, ext);
	}

	return _full_screenshot_name;
}

/** Make a screenshot of the current screen. */
static bool MakeSmallScreenshot()
{
	const ScreenshotFormat *sf = _screenshot_formats + _cur_screenshot_format;
	return sf->proc(MakeScreenshotName(SCREENSHOT_NAME, sf->extension), CurrentScreenCallback, NULL, _screen.width, _screen.height,
			BlitterFactoryBase::GetCurrentBlitter()->GetScreenDepth(), _cur_palette);
}

/** Make a zoomed-in screenshot of the currently visible area. */
static bool MakeZoomedInScreenshot()
{
	Window *w = FindWindowById(WC_MAIN_WINDOW, 0);
	ViewPort vp;

	vp.zoom = ZOOM_LVL_WORLD_SCREENSHOT;
	vp.left = w->viewport->left;
	vp.top = w->viewport->top;
	vp.virtual_left = w->viewport->virtual_left;
	vp.virtual_top = w->viewport->virtual_top;
	vp.virtual_width = w->viewport->virtual_width;
	vp.width = vp.virtual_width;
	vp.virtual_height = w->viewport->virtual_height;
	vp.height = vp.virtual_height;

	const ScreenshotFormat *sf = _screenshot_formats + _cur_screenshot_format;
	return sf->proc(MakeScreenshotName(SCREENSHOT_NAME, sf->extension), LargeWorldCallback, &vp, vp.width, vp.height,
			BlitterFactoryBase::GetCurrentBlitter()->GetScreenDepth(), _cur_palette);
}

/** Make a screenshot of the whole map. */
static bool MakeWorldScreenshot()
{
	ViewPort vp;
	const ScreenshotFormat *sf;

	vp.zoom = ZOOM_LVL_WORLD_SCREENSHOT;
	vp.left = 0;
	vp.top = 0;
	vp.virtual_left = -(int)MapMaxX() * TILE_PIXELS;
	vp.virtual_top = 0;
	vp.virtual_width = (MapMaxX() + MapMaxY()) * TILE_PIXELS;
	vp.width = vp.virtual_width;
	vp.virtual_height = (MapMaxX() + MapMaxY()) * TILE_PIXELS >> 1;
	vp.height = vp.virtual_height;

	sf = _screenshot_formats + _cur_screenshot_format;
	return sf->proc(MakeScreenshotName(SCREENSHOT_NAME, sf->extension), LargeWorldCallback, &vp, vp.width, vp.height,
			BlitterFactoryBase::GetCurrentBlitter()->GetScreenDepth(), _cur_palette);
}

/**
 * Callback for generating a heightmap. Supports 8bpp grayscale only.
 * @param userdata Pointer to user data.
 * @param buf      Destination buffer.
 * @param y        Line number of the first line to write.
 * @param pitch    Number of pixels to write (1 byte for 8bpp, 4 bytes for 32bpp). @see Colour
 * @param n        Number of lines to write.
 * @see ScreenshotCallback
 */
static void HeightmapCallback(void *userdata, void *buffer, uint y, uint pitch, uint n)
{
	byte *buf = (byte *)buffer;
	while (n > 0) {
		TileIndex ti = TileXY(MapMaxX(), y);
		for (uint x = MapMaxX(); true; x--) {
			*buf = 16 * TileHeight(ti);
			buf++;
			if (x == 0) break;
			ti = TILE_ADDXY(ti, -1, 0);
		}
		y++;
		n--;
	}
}

/**
 * Make a heightmap of the current map.
 * @param filename Filename to use for saving.
 */
bool MakeHeightmapScreenshot(const char *filename)
{
	Colour palette[256];
	for (uint i = 0; i < lengthof(palette); i++) {
		palette[i].r = i;
		palette[i].g = i;
		palette[i].b = i;
	}
	const ScreenshotFormat *sf = _screenshot_formats + _cur_screenshot_format;
	return sf->proc(filename, HeightmapCallback, NULL, MapSizeX(), MapSizeY(), 8, palette);
}

/**
 * Make an actual screenshot.
 * @param t    the type of screenshot to make.
 * @param name the name to give to the screenshot.
 * @return true iff the screenshow was made succesfully
 */
bool MakeScreenshot(ScreenshotType t, const char *name)
{
	_screenshot_name[0] = '\0';
	if (name != NULL) strecpy(_screenshot_name, name, lastof(_screenshot_name));

	bool ret;
	switch (t) {
		case SC_VIEWPORT:
			UndrawMouseCursor();
			DrawDirtyBlocks();
			ret = MakeSmallScreenshot();
			break;

		case SC_ZOOMEDIN:
			ret = MakeZoomedInScreenshot();
			break;

		case SC_WORLD:
			ret = MakeWorldScreenshot();
			break;

		case SC_HEIGHTMAP: {
			const ScreenshotFormat *sf = _screenshot_formats + _cur_screenshot_format;
			ret = MakeHeightmapScreenshot(MakeScreenshotName(HEIGHTMAP_NAME, sf->extension));
			break;
		}

		case SC_MINIMAP:
			ret = MakeMinimapWorldScreenshot();
			break;

		default:
			NOT_REACHED();
	}

	if (ret) {
		SetDParamStr(0, _screenshot_name);
		ShowErrorMessage(STR_031B_SCREENSHOT_SUCCESSFULLY, INVALID_STRING_ID, 0, 0);
	} else {
		ShowErrorMessage(STR_031C_SCREENSHOT_FAILED, INVALID_STRING_ID, 0, 0);
	}

	return ret;
}


/**
 * Return the owner of a tile to display it with in the small map in mode "Owner".
 *
 * @param tile The tile of which we would like to get the colour.
 * @return The owner of tile in the small map in mode "Owner"
 */
static Owner GetMinimapOwner(TileIndex tile)
{
	Owner o;

	if (IsTileType(tile, MP_VOID)) {
		return OWNER_END;
	} else {
		switch (GetTileType(tile)) {
		case MP_INDUSTRY: o = OWNER_DEITY;        break;
		case MP_HOUSE:    o = OWNER_TOWN;         break;
		default:          o = GetTileOwner(tile); break;
			/* FIXME: For MP_ROAD there are multiple owners.
			 * GetTileOwner returns the rail owner (level crossing) resp. the owner of ROADTYPE_ROAD (normal road),
			 * even if there are no ROADTYPE_ROAD bits on the tile.
			 */
		}

		return o;
	}
}

static void MinimapScreenCallback(void *userdata, void *buf, uint y, uint pitch, uint n)
{
	uint32 *ubuf;
	uint num, row, col;
	byte val;
	byte owner_colours[OWNER_END + 1];

	/* Fill with the player colours */
	Player *p;
	FOR_ALL_PLAYERS(p) {
		owner_colours[p->index] = MKCOLOUR(_colour_gradient[_player_colors[p->index]][5]);
	}

	/* Fill with some special colours */
	owner_colours[OWNER_TOWN]    = PC_DARK_RED;
	owner_colours[OWNER_NONE]    = PC_GRASS_LAND;
	owner_colours[OWNER_WATER]   = PC_WATER;
	owner_colours[OWNER_DEITY]   = PC_DARK_GREY; // industry
	owner_colours[OWNER_END]     = PC_BLACK;

	ubuf = (uint32 *)buf;
	num = (pitch * n);
	for (uint i = 0; i < num; i++) {
		row = y + (int)(i / pitch);
		col = (MapSizeX() - 1) - (i % pitch);

		TileIndex tile = TileXY(col, row);
		Owner o = GetMinimapOwner(tile);
		val = owner_colours[o];

		uint32 colour_buf = 0;
		colour_buf  = (_cur_palette[val].b << 0);
		colour_buf |= (_cur_palette[val].g << 8);
		colour_buf |= (_cur_palette[val].r << 16);

		*ubuf = colour_buf;
		*ubuf++;   // Skip alpha
	}
}

/**
 * Make a minimap screenshot.
 */
bool MakeMinimapWorldScreenshot()
{
	const ScreenshotFormat *sf = _screenshot_formats + _cur_screenshot_format;
	return sf->proc(MakeScreenshotName(SCREENSHOT_NAME, sf->extension), MinimapScreenCallback, nullptr, MapSizeX(), MapSizeY(), 32, _cur_palette);
}

/**
 * Make a single-window screenshot.
 */
bool MakeWindowScreenshot(Window *w, const char *filename)
{
	_screenshot_name[0] = '\0';
	if (filename != NULL) strecpy(_screenshot_name, filename, lastof(_screenshot_name));

	UndrawMouseCursor();

	const ScreenshotFormat *sf = _screenshot_formats + _cur_screenshot_format;

	return sf->proc(
		MakeScreenshotName(SCREENSHOT_NAME, sf->extension),
		CurrentScreenCallback, w, w->width, w->height,
		BlitterFactoryBase::GetCurrentBlitter()->GetScreenDepth(),
		_cur_palette
	);
}
