/* $Id$ */

/******************************************************************************
 *                             Cocoa video driver                             *
 * Known things left to do:                                                   *
 *  Nothing at the moment.                                                    *
 ******************************************************************************/

#ifdef WITH_COCOA

#include "../../stdafx.h"

#define Rect  OTTDRect
#define Point OTTDPoint
#import <Cocoa/Cocoa.h>
#undef Rect
#undef Point

#include "../../openttd.h"
#include "../../debug.h"
#include "../../variables.h"
#include "../../core/geometry_type.hpp"
#include "../../core/sort_func.hpp"
#include "cocoa_v.h"
#include "../../blitter/factory.hpp"
#include "../../fileio.h"
#include "../../gfx_func.h"
#include "../../functions.h"

#import <sys/param.h> /* for MAXPATHLEN */

/**
 * Important notice regarding all modifications!!!!!!!
 * There are certain limitations because the file is objective C++.
 * gdb has limitations.
 * C++ and objective C code can't be joined in all cases (classes stuff).
 * Read http://developer.apple.com/releasenotes/Cocoa/Objective-C++.html for more information.
 */


/* Portions of CPS.h */
struct CPSProcessSerNum {
	UInt32 lo;
	UInt32 hi;
};

extern "C" OSErr CPSGetCurrentProcess(CPSProcessSerNum* psn);
extern "C" OSErr CPSEnableForegroundOperation(CPSProcessSerNum* psn, UInt32 _arg2, UInt32 _arg3, UInt32 _arg4, UInt32 _arg5);
extern "C" OSErr CPSSetFrontProcess(CPSProcessSerNum* psn);

/* Disables a warning. This is needed since the method exists but has been dropped from the header, supposedly as of 10.4. */
#if (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_4)
@interface NSApplication(NSAppleMenu)
- (void)setAppleMenu:(NSMenu *)menu;
@end
#endif


@interface OTTDMain : NSObject
@end


static NSAutoreleasePool *_ottd_autorelease_pool;
static OTTDMain *_ottd_main;
static bool _cocoa_video_started = false;
static bool _cocoa_video_dialog = false;

CocoaSubdriver* _cocoa_subdriver = NULL;



/* The main class of the application, the application's delegate */
@implementation OTTDMain
/* Called when the internal event loop has just started running */
- (void) applicationDidFinishLaunching: (NSNotification*) note
{
	/* Hand off to main application code */
	QZ_GameLoop();

	/* We're done, thank you for playing */
	[ NSApp stop:_ottd_main ];
}

/* Display the in game quit confirmation dialog */
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*) sender
{

	HandleExitGameRequest();

	return NSTerminateCancel; // NSTerminateLater ?
}
@end

static void setApplicationMenu()
{
	/* warning: this code is very odd */
	NSMenu *appleMenu;
	NSMenuItem *menuItem;
	NSString *title;
	NSString *appName;

	appName = @"OTTD";
	appleMenu = [[NSMenu alloc] initWithTitle:appName];

	/* Add menu items */
	title = [@"About " stringByAppendingString:appName];
	[appleMenu addItemWithTitle:title action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];

	[appleMenu addItem:[NSMenuItem separatorItem]];

	title = [@"Hide " stringByAppendingString:appName];
	[appleMenu addItemWithTitle:title action:@selector(hide:) keyEquivalent:@"h"];

	menuItem = (NSMenuItem*)[appleMenu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
	[menuItem setKeyEquivalentModifierMask:(NSAlternateKeyMask|NSCommandKeyMask)];

	[appleMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];

	[appleMenu addItem:[NSMenuItem separatorItem]];

	title = [@"Quit " stringByAppendingString:appName];
	[appleMenu addItemWithTitle:title action:@selector(terminate:) keyEquivalent:@"q"];


	/* Put menu into the menubar */
	menuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
	[menuItem setSubmenu:appleMenu];
	[[NSApp mainMenu] addItem:menuItem];

	/* Tell the application object that this is now the application menu */
	[NSApp setAppleMenu:appleMenu];

	/* Finally give up our references to the objects */
	[appleMenu release];
	[menuItem release];
}

/* Create a window menu */
static void setupWindowMenu()
{
	NSMenu* windowMenu;
	NSMenuItem* windowMenuItem;
	NSMenuItem* menuItem;

	windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];

	/* "Minimize" item */
	menuItem = [[NSMenuItem alloc] initWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
	[windowMenu addItem:menuItem];
	[menuItem release];

	/* Put menu into the menubar */
	windowMenuItem = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
	[windowMenuItem setSubmenu:windowMenu];
	[[NSApp mainMenu] addItem:windowMenuItem];

	if (MacOSVersionIsAtLeast(10, 7, 0)) {
		/* The OS will change the name of this menu item automatically */
		[ windowMenu addItemWithTitle:@"Fullscreen" action:@selector(toggleFullScreen:) keyEquivalent:@"^f" ];
	}

	/* Tell the application object that this is now the window menu */
	[NSApp setWindowsMenu:windowMenu];

	/* Finally give up our references to the objects */
	[windowMenu release];
	[windowMenuItem release];
}

static void setupApplication()
{
	CPSProcessSerNum PSN;

	/* Ensure the application object is initialised */
	[NSApplication sharedApplication];

	/* Tell the dock about us */
	if (!CPSGetCurrentProcess(&PSN) &&
			!CPSEnableForegroundOperation(&PSN, 0x03, 0x3C, 0x2C, 0x1103) &&
			!CPSSetFrontProcess(&PSN)) {
		[NSApplication sharedApplication];
	}

	/* Set up the menubar */
	[NSApp setMainMenu:[[NSMenu alloc] init]];
	setApplicationMenu();
	setupWindowMenu();

	/* Create OTTDMain and make it the app delegate */
	_ottd_main = [[OTTDMain alloc] init];
	[NSApp setDelegate:_ottd_main];
}


static int CDECL ModeSorter(const OTTD_Point *p1, const OTTD_Point *p2)
{
	if (p1->x < p2->x) return -1;
	if (p1->x > p2->x) return +1;
	if (p1->y < p2->y) return -1;
	if (p1->y > p2->y) return +1;
	return 0;
}

static void QZ_GetDisplayModeInfo(CFArrayRef modes, CFIndex i, int &bpp, uint16 &width, uint16 &height)
{
	bpp = 0;
	width = 0;
	height = 0;

#if (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_6)
	if (MacOSVersionIsAtLeast(10, 6, 0)) {
		CGDisplayModeRef mode = (CGDisplayModeRef)CFArrayGetValueAtIndex(modes, i);

		width = (uint16)CGDisplayModeGetWidth(mode);
		height = (uint16)CGDisplayModeGetHeight(mode);

#if (MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_11)
		/* Extract bit depth from mode string. */
		CFStringRef pixEnc = CGDisplayModeCopyPixelEncoding(mode);
		if (CFStringCompare(pixEnc, CFSTR(IO32BitDirectPixels), kCFCompareCaseInsensitive) == kCFCompareEqualTo) bpp = 32;
		if (CFStringCompare(pixEnc, CFSTR(IO16BitDirectPixels), kCFCompareCaseInsensitive) == kCFCompareEqualTo) bpp = 16;
		if (CFStringCompare(pixEnc, CFSTR(IO8BitIndexedPixels), kCFCompareCaseInsensitive) == kCFCompareEqualTo) bpp = 8;
		CFRelease(pixEnc);
#else
		/* CGDisplayModeCopyPixelEncoding is deprecated on OSX 10.11+, but there are no 8 bpp modes anyway... */
		bpp = 32;
#endif
	} else
#endif
	{
		int intvalue;

		CFDictionaryRef onemode = (const __CFDictionary*)CFArrayGetValueAtIndex(modes, i);
		CFNumberRef number = (const __CFNumber*)CFDictionaryGetValue(onemode, kCGDisplayBitsPerPixel);
		CFNumberGetValue(number, kCFNumberSInt32Type, &intvalue);
		bpp = intvalue;

		number = (const __CFNumber*)CFDictionaryGetValue(onemode, kCGDisplayWidth);
		CFNumberGetValue(number, kCFNumberSInt32Type, &intvalue);
		width = (uint16)intvalue;

		number = (const __CFNumber*)CFDictionaryGetValue(onemode, kCGDisplayHeight);
		CFNumberGetValue(number, kCFNumberSInt32Type, &intvalue);
		height = (uint16)intvalue;
	}
}

uint QZ_ListModes(OTTD_Point *modes, uint max_modes, CGDirectDisplayID display_id, int device_depth)
{
#if (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_6) && (MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_6)
	CFArrayRef mode_list = MacOSVersionIsAtLeast(10, 6, 0) ? CGDisplayCopyAllDisplayModes(display_id, NULL) : CGDisplayAvailableModes(display_id);
#elif (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_6)
	CFArrayRef mode_list = CGDisplayCopyAllDisplayModes(display_id, NULL);
#else
	CFArrayRef mode_list = CGDisplayAvailableModes(display_id);
#endif
	CFIndex    num_modes = CFArrayGetCount(mode_list);

	/* Build list of modes with the requested bpp */
	uint count = 0;
	for (CFIndex i = 0; i < num_modes && count < max_modes; i++) {
		int bpp;
		uint16 width, height;

		QZ_GetDisplayModeInfo(mode_list, i, bpp, width, height);

		if (bpp != device_depth) continue;

		/* Check if mode is already in the list */
		bool hasMode = false;
		for (uint i = 0; i < count; i++) {
			if (modes[i].x == width &&  modes[i].y == height) {
				hasMode = true;
				break;
			}
		}

		if (hasMode) continue;

		/* Add mode to the list */
		modes[count].x = width;
		modes[count].y = height;
		count++;
	}

	/* Sort list smallest to largest */
	QSortT(modes, count, &ModeSorter);

#if (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_6)
	if (MacOSVersionIsAtLeast(10, 6, 0)) CFRelease(mode_list);
#endif

	return count;
}

/** Small function to test if the main display can display 8 bpp in fullscreen */
bool QZ_CanDisplay8bpp()
{
	/* 8bpp modes are deprecated starting in 10.5. CoreGraphics will return them
	 * as available in the display list, but many features (e.g. palette animation)
	 * will be broken. */
	if (MacOSVersionIsAtLeast(10, 5, 0)) return false;

	OTTD_Point p;

	/* We want to know if 8 bpp is possible in fullscreen and not anything about
	 * resolutions. Because of this we want to fill a list of 1 resolution of 8 bpp
	 * on display 0 (main) and return if we found one. */
	return QZ_ListModes(&p, 1, 0, 8);
}

/**
 * Update the video modus.
 *
 * @pre _cocoa_subdriver != NULL
 */
static void QZ_UpdateVideoModes()
{
	uint i, count;
	OTTD_Point modes[32];

	assert(_cocoa_subdriver != NULL);

	count = _cocoa_subdriver->ListModes(modes, lengthof(modes));

	for (i = 0; i < count; i++) {
		_resolutions[i][0] = modes[i].x;
		_resolutions[i][1] = modes[i].y;
	}

	_num_resolutions = count;
}


void QZ_GameSizeChanged()
{
	if (_cocoa_subdriver == NULL) return;

	/* Tell the game that the resolution has changed */
	_screen.width = _cocoa_subdriver->GetWidth();
	_screen.height = _cocoa_subdriver->GetHeight();
	_screen.pitch = _cocoa_subdriver->GetWidth();
	_fullscreen = _cocoa_subdriver->IsFullscreen();

	BlitterFactoryBase::GetCurrentBlitter()->PostResize();

	GameSizeChanged();
}


static CocoaSubdriver *QZ_CreateWindowSubdriver(int width, int height, int bpp)
{
	CocoaSubdriver *ret;

#if defined(ENABLE_COCOA_QUARTZ) && (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_4)
	/* The reason for the version mismatch is due to the fact that the 10.4 binary needs to work on 10.5 as well. */
	if (MacOSVersionIsAtLeast(10, 5, 0)) {
		ret = QZ_CreateWindowQuartzSubdriver(width, height, bpp);
		if (ret != NULL) return ret;
	}
#endif

#ifdef ENABLE_COCOA_QUICKDRAW
	ret = QZ_CreateWindowQuickdrawSubdriver(width, height, bpp);
	if (ret != NULL) return ret;
#endif

#if defined(ENABLE_COCOA_QUARTZ) && (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_4)
	/*
	 * If we get here we are running 10.4 or earlier and either openttd was compiled without the QuickDraw driver
	 * or it failed to load for some reason. Fall back to Quartz if possible even though that driver is slower.
	 */
        if (MacOSVersionIsAtLeast(10, 4, 0)) {
                ret = QZ_CreateWindowQuartzSubdriver(width, height, bpp);
                if (ret != NULL) return ret;
        }
#endif

	return NULL;
}

/**
 * Find a suitable cocoa subdriver.
 *
 * @param width Width of display area.
 * @param height Height of display area.
 * @param bpp Colour depth of display area.
 * @param fullscreen Whether a fullscreen mode is requested.
 * @param fallback Whether we look for a fallback driver.
 * @return Pointer to window subdriver.
 */
static CocoaSubdriver *QZ_CreateSubdriver(int width, int height, int bpp, bool fullscreen, bool fallback)
{
	CocoaSubdriver *ret = NULL;
	/* OSX 10.7 allows to toggle fullscreen mode differently */
	if (MacOSVersionIsAtLeast(10, 7, 0)) {
		ret = QZ_CreateWindowSubdriver(width, height, bpp);
		if (ret != NULL && fullscreen) ret->ToggleFullscreen();
	}
#if (MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_9)
	else {
		ret = fullscreen ? QZ_CreateFullscreenSubdriver(width, height, bpp) : QZ_CreateWindowSubdriver(width, height, bpp);
	}
#endif /* (MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_9) */

	if (ret != NULL) {
			/* We cannot set any fullscreen mode on OSX 10.7 when not compiled against SDK 10.7 */
#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_7
		if (fullscreen) { ret->ToggleFullscreen(); }
#endif
		return ret;
	}

	if (!fallback) return NULL;

	/* Try again in 640x480 windowed */
	DEBUG(driver, 0, "Setting video mode failed, falling back to 640x480 windowed mode.");
	ret = QZ_CreateWindowSubdriver(640, 480, bpp);
	if (ret != NULL) return ret;

#if defined(_DEBUG) && (MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_9)
	/* This Fullscreen mode crashes on OSX 10.7 */
	if !(MacOSVersionIsAtLeast(10, 7, 0) {
		/* Try fullscreen too when in debug mode */
		DEBUG(driver, 0, "Setting video mode failed, falling back to 640x480 fullscreen mode.");
		ret = QZ_CreateFullscreenSubdriver(640, 480, bpp);
		if (ret != NULL) return ret;
	}
#endif /* defined(_DEBUG) && (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_9) */

	return NULL;
}


static FVideoDriver_Cocoa iFVideoDriver_Cocoa;

void VideoDriver_Cocoa::Stop()
{
	if (!_cocoa_video_started) return;

	delete _cocoa_subdriver;
	_cocoa_subdriver = NULL;

	[_ottd_main release];

	_cocoa_video_started = false;
}

const char *VideoDriver_Cocoa::Start(const char * const *parm)
{
	int width, height, bpp;

	if (!MacOSVersionIsAtLeast(10, 3, 0)) return "The Cocoa video driver requires Mac OS X 10.3 or later.";

	if (_cocoa_video_started) return "Already started";
	_cocoa_video_started = true;

	setupApplication();

	/* Don't create a window or enter fullscreen if we're just going to show a dialog. */
	if (_cocoa_video_dialog) return NULL;

	width = _cur_resolution[0];
	height = _cur_resolution[1];
	bpp = BlitterFactoryBase::GetCurrentBlitter()->GetScreenDepth();

	_cocoa_subdriver = QZ_CreateSubdriver(width, height, bpp, _fullscreen, true);
	if (_cocoa_subdriver == NULL) {
		Stop();
		return "Could not create subdriver";
	}

	QZ_GameSizeChanged();

	QZ_UpdateVideoModes();

	return NULL;
}

void VideoDriver_Cocoa::MakeDirty(int left, int top, int width, int height)
{
	assert(_cocoa_subdriver != NULL);

	_cocoa_subdriver->MakeDirty(left, top, width, height);
}

void VideoDriver_Cocoa::MainLoop()
{
	/* Start the main event loop */
	[NSApp run];
}

bool VideoDriver_Cocoa::ChangeResolution(int w, int h)
{
	bool ret;

	assert(_cocoa_subdriver != NULL);

	ret = _cocoa_subdriver->ChangeResolution(w, h);

	QZ_GameSizeChanged();

	QZ_UpdateVideoModes();

	return ret;
}

bool VideoDriver_Cocoa::ToggleFullscreen(bool full_screen)
{
	assert(_cocoa_subdriver != NULL);

	/* For 10.7 and later, we try to toggle using the quartz subdriver. */
	if (_cocoa_subdriver->ToggleFullscreen()) return true;

	bool oldfs = _cocoa_subdriver->IsFullscreen();
	if (full_screen != oldfs) {
		int width  = _cocoa_subdriver->GetWidth();
		int height = _cocoa_subdriver->GetHeight();
		int bpp    = BlitterFactoryBase::GetCurrentBlitter()->GetScreenDepth();

		delete _cocoa_subdriver;
		_cocoa_subdriver = NULL;

		_cocoa_subdriver = QZ_CreateSubdriver(width, height, bpp, full_screen, false);
		if (_cocoa_subdriver == NULL) {
			_cocoa_subdriver = QZ_CreateSubdriver(width, height, bpp, oldfs, true);
			if (_cocoa_subdriver == NULL) error("Cocoa: Failed to create subdriver");
		}
	}

	QZ_GameSizeChanged();

	QZ_UpdateVideoModes();
	return _cocoa_subdriver->IsFullscreen() == full_screen;
}

/* This is needed since sometimes assert is called before the videodriver is initialized */
void CocoaDialog(const char* title, const char* message, const char* buttonLabel)
{
	bool wasstarted;

	_cocoa_video_dialog = true;

	wasstarted = _cocoa_video_started;
	if (_video_driver == NULL) {
		setupApplication(); // Setup application before showing dialog
	} else if (!_cocoa_video_started && _video_driver->Start(NULL) != NULL) {
		fprintf(stderr, "%s: %s\n", title, message);
		return;
	}

	NSRunAlertPanel([ NSString stringWithUTF8String:title ], [ NSString stringWithUTF8String:message ], [ NSString stringWithUTF8String:buttonLabel ], nil, nil);

	if (!wasstarted && _video_driver != NULL) _video_driver->Stop();

	_cocoa_video_dialog = false;
}

/* This is needed since OS X application bundles do not have a
 * current directory and the data files are 'somewhere' in the bundle */
void cocoaSetApplicationBundleDir()
{
	char tmp[MAXPATHLEN];
	CFURLRef url = CFBundleCopyResourcesDirectoryURL(CFBundleGetMainBundle());
	if (CFURLGetFileSystemRepresentation(url, true, (unsigned char*)tmp, MAXPATHLEN)) {
		AppendPathSeparator(tmp, lengthof(tmp));
		_searchpaths[SP_APPLICATION_BUNDLE_DIR] = strdup(tmp);
	} else {
		_searchpaths[SP_APPLICATION_BUNDLE_DIR] = NULL;
	}

	CFRelease(url);
}

/* These are called from main() to prevent a _NSAutoreleaseNoPool error when
 * exiting before the cocoa video driver has been loaded
 */
void cocoaSetupAutoreleasePool()
{
	_ottd_autorelease_pool = [[NSAutoreleasePool alloc] init];
}

void cocoaReleaseAutoreleasePool()
{
	[_ottd_autorelease_pool release];
}


/**
 * Re-implement the system cursor in order to allow hiding and showing it nicely
 */
@implementation NSCursor (OTTD_CocoaCursor)
+ (NSCursor *) clearCocoaCursor
{
	/* RAW 16x16 transparent GIF */
	unsigned char clearGIFBytes[] = {
		0x47, 0x49, 0x46, 0x38, 0x37, 0x61, 0x10, 0x00, 0x10, 0x00, 0x80, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0xF9, 0x04, 0x01, 0x00,
		0x00, 0x01, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00,
		0x00, 0x02, 0x0E, 0x8C, 0x8F, 0xA9, 0xCB, 0xED, 0x0F, 0xA3, 0x9C, 0xB4,
		0xDA, 0x8B, 0xB3, 0x3E, 0x05, 0x00, 0x3B};
	NSData *clearGIFData = [ NSData dataWithBytesNoCopy:&clearGIFBytes[0] length:55 freeWhenDone:NO ];
	NSImage *clearImg = [ [ NSImage alloc ] initWithData:clearGIFData ];
	return [ [ NSCursor alloc ] initWithImage:clearImg hotSpot:NSMakePoint(0.0,0.0) ];
}
@end



@implementation OTTD_CocoaWindow

- (void)setDriver:(CocoaSubdriver*)drv
{
	driver = drv;
}
/**
  * Minimize the window
  */
- (void)miniaturize:(id)sender
{
	/* make the alpha channel opaque so anim won't have holes in it */
	driver->SetPortAlphaOpaque();

	/* window is hidden now */
	driver->active = false;

	[ super miniaturize:sender ];
}

/**
 * This method fires just before the window deminaturizes from the Dock.
 * We'll save the current visible surface, let the window manager redraw any
 * UI elements, and restore the surface. This way, no expose event
 * is required, and the deminiaturize works perfectly.
 */
- (void)display
{
	driver->SetPortAlphaOpaque();

	/* save current visible surface */
	[ self cacheImageInRect:[ driver->cocoaview frame ] ];

	/* let the window manager redraw controls, border, etc */
	[ super display ];

	/* restore visible surface */
	[ self restoreCachedImage ];

	/* window is visible again */
	driver->active = true;
}
/**
 * Define the rectangle we draw our window in
 */
- (void)setFrame:(NSRect)frameRect display:(BOOL)flag
{
	[ super setFrame:frameRect display:flag ];

	/* Don't do anything if the window is currently being created */
	if (driver->setup) return;

	if (!driver->WindowResized()) error("Cocoa: Failed to resize window.");
}
/**
 * Handle hiding of the application
 */
- (void)appDidHide:(NSNotification*)note
{
	driver->active = false;
}
/**
 * Fade-in the application and restore display plane
 */
- (void)appWillUnhide:(NSNotification*)note
{
	driver->SetPortAlphaOpaque ();

	/* save current visible surface */
	[ self cacheImageInRect:[ driver->cocoaview frame ] ];
}
/**
 * Unhide and restore display plane and re-activate driver
 */
- (void)appDidUnhide:(NSNotification*)note
{
	/* restore cached image, since it may not be current, post expose event too */
	[ self restoreCachedImage ];

	driver->active = true;
}
/**
 * Initialize event system for the application rectangle
 */
- (id)initWithContentRect:(NSRect)contentRect styleMask:(unsigned int)styleMask backing:(NSBackingStoreType)backingType defer:(BOOL)flag
{
	/* Make our window subclass receive these application notifications */
	[ [ NSNotificationCenter defaultCenter ] addObserver:self
		selector:@selector(appDidHide:) name:NSApplicationDidHideNotification object:NSApp ];

	[ [ NSNotificationCenter defaultCenter ] addObserver:self
		selector:@selector(appDidUnhide:) name:NSApplicationDidUnhideNotification object:NSApp ];

	[ [ NSNotificationCenter defaultCenter ] addObserver:self
		selector:@selector(appWillUnhide:) name:NSApplicationWillUnhideNotification object:NSApp ];

	return [ super initWithContentRect:contentRect styleMask:styleMask backing:backingType defer:flag ];
}

@end




@implementation OTTD_CocoaView
/**
 * Initialize the driver
 */
- (void)setDriver:(CocoaSubdriver*)drv
{
	driver = drv;
}
/**
 * Define the opaqueness of the window / screen
 * @return opaqueness of window / screen
 */
- (BOOL)isOpaque
{
	return YES;
}
/**
 * Draws a rectangle on the screen.
 * It's overwritten by the individual drivers but must be defined
 */
- (void)drawRect:(NSRect)invalidRect
{
	return;
}
/**
 * Allow to handle events
 */
- (BOOL)acceptsFirstResponder
{
	return YES;
}
/**
 * Actually handle events
 */
- (BOOL)becomeFirstResponder
{
	return YES;
}
/**
 * Define the rectangle where we draw our application window
 */
- (void)setTrackingRect
{
	NSPoint loc = [ self convertPoint:[ [ self window ] mouseLocationOutsideOfEventStream ] fromView:nil ];
	BOOL inside = ([ self hitTest:loc ]==self);
	if (inside) [ [ self window ] makeFirstResponder:self ];
	trackingtag = [ self addTrackingRect:[ self visibleRect ] owner:self userData:nil assumeInside:inside ];
}
/**
 * Return responsibility for the application window to system
 */
- (void)clearTrackingRect
{
	[ self removeTrackingRect:trackingtag ];
}
/**
 * Declare responsibility for the cursor within our application rect
 */
- (void)resetCursorRects
{
	[ super resetCursorRects ];
	[ self clearTrackingRect ];
	[ self setTrackingRect ];
	[ self addCursorRect:[ self bounds ] cursor:[ NSCursor clearCocoaCursor ] ];
}
/**
 * Prepare for moving the application window
 */
- (void)viewWillMoveToWindow:(NSWindow *)win
{
	if (!win && [ self window ]) [ self clearTrackingRect ];
}
/**
 * Restore our responsibility for our application window after moving
 */
- (void)viewDidMoveToWindow
{
	if ([ self window ]) [ self setTrackingRect ];
}
/**
 * Make OpenTTD aware that it has control over the mouse
 */
- (void)mouseEntered:(NSEvent *)theEvent
{
	_cursor.in_window = true;
}
/**
 * Make OpenTTD aware that it has NOT control over the mouse
 */
- (void)mouseExited:(NSEvent *)theEvent
{
	if (_cocoa_subdriver != NULL) UndrawMouseCursor();
	_cursor.in_window = false;
}
@end



@implementation OTTD_CocoaWindowDelegate
/** Initialize the video driver */
- (void)setDriver:(CocoaSubdriver*)drv
{
	driver = drv;
}
/** Handle closure requests */
- (BOOL)windowShouldClose:(id)sender
{
	HandleExitGameRequest();

	return NO;
}
/** Handle key acceptance */
- (void)windowDidBecomeKey:(NSNotification*)aNotification
{
	driver->active = true;
}
/** Resign key acceptance */
- (void)windowDidResignKey:(NSNotification*)aNotification
{
	driver->active = false;
}
/** Handle becoming main window */
- (void)windowDidBecomeMain:(NSNotification*)aNotification
{
	driver->active = true;
}
/** Resign being main window */
- (void)windowDidResignMain:(NSNotification*)aNotification
{
	driver->active = false;
}

@end

#endif /* WITH_COCOA */
