/* $Id$ */

/******************************************************************************
 *                             Cocoa video driver                             *
 * Known things left to do:                                                   *
 *  List available resolutions.                                               *
 ******************************************************************************/

#ifdef WITH_COCOA
#ifdef ENABLE_COCOA_QUARTZ

#include "../../stdafx.h"

#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_4

#define Rect  OTTDRect
#define Point OTTDPoint
#import <Cocoa/Cocoa.h>
#undef Rect
#undef Point

#include "../../debug.h"
#include "../../core/geometry_type.hpp"
#include "cocoa_v.h"
#include "../../core/math_func.hpp"
#include "../../gfx_func.h"

/**
 * Important notice regarding all modifications!!!!!!!
 * There are certain limitations because the file is objective C++.
 * gdb has limitations.
 * C++ and objective C code can't be joined in all cases (classes stuff).
 * Read http://developer.apple.com/releasenotes/Cocoa/Objective-C++.html for more information.
 */

class WindowQuartzSubdriver;

/* Subclass of OTTD_CocoaView to fix Quartz rendering */
@interface OTTD_QuartzView : OTTD_CocoaView
- (void)setDriver:(WindowQuartzSubdriver*)drv;
- (void)drawRect:(NSRect)invalidRect;
@end

class WindowQuartzSubdriver: public CocoaSubdriver {
private:
	/**
	 * This function copies 8bpp pixels from the screen buffer in 32bpp windowed mode.
	 *
	 * @param left The x coord for the left edge of the box to blit.
	 * @param top The y coord for the top edge of the box to blit.
	 * @param right The x coord for the right edge of the box to blit.
	 * @param bottom The y coord for the bottom edge of the box to blit.
	 */
	void BlitIndexedToView32(int left, int top, int right, int bottom);

	virtual void GetDeviceInfo();
	virtual bool SetVideoMode(int width, int height);

public:
	WindowQuartzSubdriver(int bpp);
	virtual ~WindowQuartzSubdriver();

	virtual void Draw();
	virtual void MakeDirty(int left, int top, int width, int height);
	virtual void UpdatePalette(uint first_color, uint num_colors);

	virtual uint ListModes(OTTD_Point* modes, uint max_modes);

	virtual bool ChangeResolution(int w, int h);

	virtual bool IsFullscreen() { return false; }

	virtual int GetWidth() { return window_width; }
	virtual int GetHeight() { return window_height; }
	virtual void *GetPixelBuffer() { return buffer_depth == 8 ? pixel_buffer : window_buffer; }

	/* Convert local coordinate to window server (CoreGraphics) coordinate */
	virtual CGPoint PrivateLocalToCG(NSPoint* p);

	virtual NSPoint GetMouseLocation(NSEvent *event);
	virtual bool MouseIsInsideView(NSPoint *pt);

	virtual bool IsActive() { return active; }


	void SetPortAlphaOpaque();
	bool WindowResized();
};


static CGColorSpaceRef QZ_GetCorrectColorSpace()
{
	static CGColorSpaceRef colorSpace = NULL;

	if (colorSpace == NULL) {
#if (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_5)
		if (MacOSVersionIsAtLeast(10, 5, 0)) {
			colorSpace = CGDisplayCopyColorSpace(CGMainDisplayID());
		} else
#endif
		{
#if (MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_5) && !defined(HAVE_OSX_1011_SDK)
			CMProfileRef sysProfile;
			if (CMGetSystemProfile(&sysProfile) == noErr) {
				colorSpace = CGColorSpaceCreateWithPlatformColorSpace(sysProfile);
				CMCloseProfile(sysProfile);
			}
#endif
		}

		if (colorSpace == NULL) colorSpace = CGColorSpaceCreateDeviceRGB();

		if (colorSpace == NULL) error("Could not get system colour space. You might need to recalibrate your monitor.");
	}

	return colorSpace;
}

@implementation OTTD_QuartzView

- (void)setDriver:(WindowQuartzSubdriver*)drv
{
	driver = drv;
}
- (void)drawRect:(NSRect)invalidRect
{
	CGImageRef    fullImage;
	CGImageRef    clippedImage;
	NSRect        rect;
	const NSRect *dirtyRects;
	NSInteger     dirtyRectCount;
	int           n;
	CGRect        clipRect;
	CGRect        blitRect;
	uint32        blitArea       = 0;
	NSRect        frameRect      = [ self frame ];
	CGContextRef  viewContext    = (CGContextRef)[ [ NSGraphicsContext currentContext ] graphicsPort ];

	if (driver->cgcontext == NULL) return;

	CGContextSetShouldAntialias(viewContext, FALSE);
	CGContextSetInterpolationQuality(viewContext, kCGInterpolationNone);

	/* The obtained 'rect' is actually a union of all dirty rects, let's ask for an explicit list of rects instead */
	[ self getRectsBeingDrawn:&dirtyRects count:&dirtyRectCount ];

	/* We need an Image in order to do blitting, but as we don't touch the context between this call and drawing no copying will actually be done here */
	fullImage = CGBitmapContextCreateImage(driver->cgcontext);

	/* Calculate total area we are blitting */
	for (n = 0; n < dirtyRectCount; n++) {
		blitArea += dirtyRects[n].size.width * dirtyRects[n].size.height;
 	}

	/*
	 * This might be completely stupid, but in my extremely subjective opinion it feels faster
	 * The point is, if we're blitting less than 50% of the dirty rect union then it's still a good idea to blit each dirty
	 * rect separately but if we blit more than that, it's just cheaper to blit the entire union in one pass.
	 * Feel free to remove or find an even better value than 50% ... / blackis
	 */
	if (blitArea / (float)(invalidRect.size.width * invalidRect.size.height) > 0.5f) {
		rect = invalidRect;

		blitRect.origin.x = rect.origin.x;
		blitRect.origin.y = rect.origin.y;
		blitRect.size.width = rect.size.width;
		blitRect.size.height = rect.size.height;

		clipRect.origin.x = rect.origin.x;
		clipRect.origin.y = frameRect.size.height - rect.origin.y - rect.size.height;

		clipRect.size.width = rect.size.width;
		clipRect.size.height = rect.size.height;

		/* Blit dirty part of image */
		clippedImage = CGImageCreateWithImageInRect(fullImage, clipRect);
		CGContextDrawImage(viewContext, blitRect, clippedImage);
		CGImageRelease(clippedImage);
	} else {
		for (n = 0; n < dirtyRectCount; n++) {
			rect = dirtyRects[n];

			blitRect.origin.x = rect.origin.x;
			blitRect.origin.y = rect.origin.y;
			blitRect.size.width = rect.size.width;
			blitRect.size.height = rect.size.height;

			clipRect.origin.x = rect.origin.x;
			clipRect.origin.y = frameRect.size.height - rect.origin.y - rect.size.height;

			clipRect.size.width = rect.size.width;
			clipRect.size.height = rect.size.height;

			/* Blit dirty part of image */
			clippedImage = CGImageCreateWithImageInRect(fullImage, clipRect);
			CGContextDrawImage(viewContext, blitRect, clippedImage);
			CGImageRelease(clippedImage);
 		}
 	}

	CGImageRelease(fullImage);
}

@end


extern const char _openttd_revision[];


void WindowQuartzSubdriver::GetDeviceInfo()
{
	CFDictionaryRef    cur_mode;

	/* Initialize the video settings; this data persists between mode switches */
	cur_mode = CGDisplayCurrentMode(kCGDirectMainDisplay);

	/* Gather some information that is useful to know about the display */
	CFNumberGetValue(
		(const __CFNumber*)CFDictionaryGetValue(cur_mode, kCGDisplayWidth),
		kCFNumberSInt32Type, &device_width
	);

	CFNumberGetValue(
		(const __CFNumber*)CFDictionaryGetValue(cur_mode, kCGDisplayHeight),
		kCFNumberSInt32Type, &device_height
	);
}

bool WindowQuartzSubdriver::SetVideoMode(int width, int height)
{
	char caption[50];
	NSString *nsscaption;
	unsigned int style;
	NSRect contentRect;
	bool ret;

	setup = true;

	GetDeviceInfo();

	if (width > device_width)
		width = device_width;
	if (height > device_height)
		height = device_height;

	contentRect = NSMakeRect(0, 0, width, height);

	/* Check if we should recreate the window */
	if (this->window == nil) {
		OTTD_CocoaWindowDelegate *delegate;

		/* Set the window style */
		style = NSTitledWindowMask;
		style |= (NSMiniaturizableWindowMask | NSClosableWindowMask);
		style |= NSResizableWindowMask;

		/* Manually create a window, avoids having a nib file resource */
		this->window = [ [ OTTD_CocoaWindow alloc ]
							initWithContentRect:contentRect
							styleMask:style
							backing:NSBackingStoreBuffered
							defer:NO ];

		if (window == nil) {
			DEBUG(driver, 0, "Could not create the Cocoa window.");
			setup = false;
			return false;
		}

		[ window setDriver:this ];

		snprintf(caption, sizeof(caption), "OpenTTD %s", _openttd_revision);
		nsscaption = [ [ NSString alloc ] initWithCString:caption ];
		[ window setTitle: nsscaption ];
		[ window setMiniwindowTitle: nsscaption ];
		[ nsscaption release ];

		[ window setAcceptsMouseMovedEvents: YES ];
		[ window setViewsNeedDisplay: NO ];

		[ window useOptimizedDrawing: YES ];

		delegate = [ [ OTTD_CocoaWindowDelegate alloc ] init ];
		[ delegate setDriver:this ];
		[ this->window setDelegate:[ delegate autorelease ] ];
	} else {
		/* We already have a window, just change its size */
		if (!isCustom) {
			[ window setContentSize: contentRect.size ];

			// Ensure frame height - title bar height >= view height
			contentRect.size.height = Clamp(height, 0, [ window frame ].size.height - 22 /* 22 is the height of title bar of window*/);

		if (this->cocoaview != nil) {
			height = contentRect.size.height;
			[ this->cocoaview setFrameSize:contentRect.size ];
		}
	}

	window_width = width;
	window_height = height;

	[ window center ];

	/* Only recreate the view if it doesn't already exist */
	if (this->cocoaview == nil) {
		this->cocoaview = [ [ OTTD_QuartzView alloc ] initWithFrame:contentRect ];
		if (this->cocoaview == nil) {
			DEBUG(driver, 0, "Could not create the Quickdraw view.");
			setup = false;
			return false;
		}

		[ this->cocoaview setDriver:this ];

		[ this->cocoaview setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable ];
		[ this->window setContentView:cocoaview ];
		[ this->cocoaview release ];
		[ this->window makeKeyAndOrderFront:nil ];
	}

	ret = WindowResized();

	UpdatePalette(0, 256);

	setup = false;

	return ret;
}

void WindowQuartzSubdriver::BlitIndexedToView32(int left, int top, int right, int bottom)
{
	const uint32 *pal   = this->palette;
	const uint8  *src   = (uint8*)this->pixel_buffer;
	uint32       *dst   = (uint32*)this->window_buffer;
	uint          width = this->window_width;
	uint          pitch = this->window_width;

	for (int y = top; y < bottom; y++) {
		for (int x = left; x < right; x++) {
			dst[y * pitch + x] = pal[src[y * width + x]];
		}
	}
}


WindowQuartzSubdriver::WindowQuartzSubdriver(int bpp)
{
	this->window_width  = 0;
	this->window_height = 0;
	this->buffer_depth  = bpp;
	this->window_buffer  = NULL;
	this->pixel_buffer  = NULL;
	this->active        = false;
	this->setup         = false;

	this->window = nil;
	this->cocoaview = nil;

	cgcontext = NULL;

	num_dirty_rects = MAX_DIRTY_RECTS;
}

WindowQuartzSubdriver::~WindowQuartzSubdriver()
{
	/* Release window mode resources */
	if (window != nil) [ window close ];

	CGContextRelease(cgcontext);

	free(this->window_buffer);
	free(this->pixel_buffer);
}

void WindowQuartzSubdriver::Draw()
{
	int i;
	NSRect dirtyrect;

	/* Check if we need to do anything */
	if (num_dirty_rects == 0 ||
		[ window isMiniaturized ]) {
		return;
	}

	if (num_dirty_rects >= MAX_DIRTY_RECTS) {
		num_dirty_rects = 1;
		dirty_rects[0].left = 0;
		dirty_rects[0].top = 0;
		dirty_rects[0].right = window_width;
		dirty_rects[0].bottom = window_height;
	}

	/* Build the region of dirty rectangles */
	for (i = 0; i < num_dirty_rects; i++) {
		/* We only need to blit in indexed mode since in 32bpp mode the game draws directly to the image. */
		if(buffer_depth == 8) {
			BlitIndexedToView32(
				dirty_rects[i].left,
				dirty_rects[i].top,
				dirty_rects[i].right,
				dirty_rects[i].bottom
			);
		}

		dirtyrect.origin.x = dirty_rects[i].left;
		dirtyrect.origin.y = window_height - dirty_rects[i].bottom;
		dirtyrect.size.width = dirty_rects[i].right - dirty_rects[i].left;
		dirtyrect.size.height = dirty_rects[i].bottom - dirty_rects[i].top;

		/* Normally drawRect will be automatically called by Mac OS X during next update cycle,
		 * and then blitting will occur. If force_update is true, it will be done right now. */
		[ this->cocoaview setNeedsDisplayInRect:dirtyrect ];
		if (force_update) [ this->cocoaview displayIfNeeded ];
	}

	//DrawResizeIcon();

	num_dirty_rects = 0;
}

void WindowQuartzSubdriver::MakeDirty(int left, int top, int width, int height)
{
	if (num_dirty_rects < MAX_DIRTY_RECTS) {
		dirty_rects[num_dirty_rects].left = left;
		dirty_rects[num_dirty_rects].top = top;
		dirty_rects[num_dirty_rects].right = left + width;
		dirty_rects[num_dirty_rects].bottom = top + height;
	}
	num_dirty_rects++;
}

void WindowQuartzSubdriver::UpdatePalette(uint first_color, uint num_colors)
{
	uint i;

	if (buffer_depth != 8)
		return;

	for (i = first_color; i < first_color + num_colors; i++) {
		uint32 clr = 0xff000000;
		clr |= (uint32)_cur_palette[i].r << 16;
		clr |= (uint32)_cur_palette[i].g << 8;
		clr |= (uint32)_cur_palette[i].b;
		palette[i] = clr;
	}

	num_dirty_rects = MAX_DIRTY_RECTS;
}

uint WindowQuartzSubdriver::ListModes(OTTD_Point* modes, uint max_modes)
{
	return QZ_ListModes(modes, max_modes, kCGDirectMainDisplay, buffer_depth);
}

bool WindowQuartzSubdriver::ChangeResolution(int w, int h)
{
	int old_width  = window_width;
	int old_height = window_height;

	if (SetVideoMode(w, h))
		return true;

	if (old_width != 0 && old_height != 0)
		SetVideoMode(old_width, old_height);

	return false;
}

/* Convert local coordinate to window server (CoreGraphics) coordinate */
CGPoint WindowQuartzSubdriver::PrivateLocalToCG(NSPoint* p)
{
	p->y = this->window_height - p->y;
	*p = [ this->cocoaview convertPoint:*p toView:nil ];

	*p = [ window convertBaseToScreen:*p ];
	p->y = device_height - p->y;

	return CGPointMake(p->x, p->y);
}

NSPoint WindowQuartzSubdriver::GetMouseLocation(NSEvent *event)
{
	NSPoint pt = [ event locationInWindow ];
	pt = [ this->cocoaview convertPoint:pt fromView:nil ];

	pt.y = window_height - pt.y;

	return pt;
}

bool WindowQuartzSubdriver::MouseIsInsideView(NSPoint *pt)
{
	return [ cocoaview mouse:*pt inRect:[ this->cocoaview bounds ] ];
}


/* This function makes the *game region* of the window 100% opaque.
 * The genie effect uses the alpha component. Otherwise,
 * it doesn't seem to matter what value it has.
 */
void WindowQuartzSubdriver::SetPortAlphaOpaque()
{
	uint32 *pixels = (uint32*)this->window_buffer;
	uint32  pitch  = this->window_width;

	for (int y = 0; y < window_height; y++)
		for (int x = 0; x < window_width; x++) {
		pixels[y * pitch + x] |= 0xFF000000;
	}
}

bool WindowQuartzSubdriver::WindowResized()
{
	if (this->window == nil || this->cocoaview == nil) return true;

	NSRect newframe = [ this->cocoaview frame ];

	window_width = newframe.size.width;
	window_height = newframe.size.height;

	/* Create Core Graphics Context */

	free(this->window_buffer);
	this->window_buffer = (uint32*)malloc(this->window_width * this->window_height * 4);

	CGContextRelease(this->cgcontext);
	this->cgcontext = CGBitmapContextCreate(
		this->window_buffer,        // data
		this->window_width,        // width
		this->window_height,       // height
		8,                         // bits per component
		this->window_width * 4,          // bytes per row
		QZ_GetCorrectColorSpace(), // color space
		kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Host
	);

	assert(this->cgcontext != NULL);
	CGContextSetShouldAntialias(this->cgcontext, FALSE);
	CGContextSetAllowsAntialiasing(this->cgcontext, FALSE);
	CGContextSetInterpolationQuality(this->cgcontext, kCGInterpolationNone);

	if (buffer_depth == 8) {
		free(pixel_buffer);
		pixel_buffer = malloc(window_width * window_height);
		if (pixel_buffer == NULL) {
			DEBUG(driver, 0, "Failed to allocate pixel buffer");
			return false;
		}
	}

	QZ_GameSizeChanged();

	/* Redraw screen */
	num_dirty_rects = MAX_DIRTY_RECTS;

	return true;
}


CocoaSubdriver *QZ_CreateWindowQuartzSubdriver(int width, int height, int bpp)
{
	WindowQuartzSubdriver *ret;

	if (!MacOSVersionIsAtLeast(10, 4, 0)) {
		DEBUG(driver, 0, "The cocoa quartz subdriver requires Mac OS X 10.4 or later.");
		return NULL;
	}

	if (bpp != 8 && bpp != 32) {
		DEBUG(driver, 0, "The cocoa quartz subdriver only supports 8 and 32 bpp.");
		return NULL;
	}

	ret = new WindowQuartzSubdriver(bpp);

	if (!ret->ChangeResolution(width, height)) {
		delete ret;
		return NULL;
	}

	return ret;
}


#endif /* MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_4 */
#endif /* ENABLE_COCOA_QUARTZ */
#endif /* WITH_COCOA */
