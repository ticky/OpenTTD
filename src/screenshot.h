/* $Id$ */

/** @file screenshot.h */

#ifndef SCREENSHOT_H
#define SCREENSHOT_H

void InitializeScreenshotFormats();

const char *GetScreenshotFormatDesc(int i);
void SetScreenshotFormat(uint i);
const char *GetCurrentScreenshotExtension();

/** Type of requested screenshot */
enum ScreenshotType {
	SC_VIEWPORT,    ///< Screenshot of viewport.
	SC_ZOOMEDIN,    ///< Zoomed in screenshot of the visible area.
	SC_WORLD,       ///< World screenshot.
	SC_HEIGHTMAP,   ///< Heightmap of the world.
	SC_MINIMAP,     ///< Minimap screenshot.
};

bool MakeHeightmapScreenshot(const char *filename);
bool MakeScreenshot(ScreenshotType t, const char *name);
bool MakeMinimapWorldScreenshot();

extern char _screenshot_format_name[8];
extern uint _num_screenshot_formats;
extern uint _cur_screenshot_format;
extern char _screenshot_name[128];
extern char _full_screenshot_name[MAX_PATH];

#endif /* SCREENSHOT_H */
