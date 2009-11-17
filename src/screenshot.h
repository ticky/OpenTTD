/* $Id$ */

/** @file screenshot.h */

#ifndef SCREENSHOT_H
#define SCREENSHOT_H

void InitializeScreenshotFormats();

const char *GetScreenshotFormatDesc(int i);
void SetScreenshotFormat(int i);

/** Type of requested screenshot */
enum ScreenshotType {
	SC_NONE,     ///< No screenshot requested
	SC_VIEWPORT, ///< Screenshot of viewport
	SC_WORLD,    ///< World screenshot
};

bool MakeScreenshot();
void RequestScreenshot(ScreenshotType t, const char *name);
bool IsScreenshotRequested();

extern char _screenshot_format_name[8];
extern uint _num_screenshot_formats;
extern uint _cur_screenshot_format;
extern char _screenshot_name[128];
extern char _full_screenshot_name[MAX_PATH];

#endif /* SCREENSHOT_H */
