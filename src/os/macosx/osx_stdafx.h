/* $Id$ */

#ifndef MACOS_STDAFX_H
#define MACOS_STDAFX_H


/* It would seem that to ensure backward compability we have to ensure that we have defined MAC_OS_X_VERSION_10_x everywhere */
#ifndef MAC_OS_X_VERSION_10_3
#define MAC_OS_X_VERSION_10_3 1030
#endif

#ifndef MAC_OS_X_VERSION_10_4
#define MAC_OS_X_VERSION_10_4 1040
#endif

#ifndef MAC_OS_X_VERSION_10_5
#define MAC_OS_X_VERSION_10_5 1050
#endif

#ifndef MAC_OS_X_VERSION_10_6
#define MAC_OS_X_VERSION_10_6 1060
#endif

#ifndef MAC_OS_X_VERSION_10_7
#define MAC_OS_X_VERSION_10_7 1070
#endif

#ifndef MAC_OS_X_VERSION_10_8
#define MAC_OS_X_VERSION_10_8 1080
#endif

#ifndef MAC_OS_X_VERSION_10_9
#define MAC_OS_X_VERSION_10_9 1090
#endif


#define __STDC_LIMIT_MACROS
#include <stdint.h>

/* Some gcc versions include assert.h via this header. As this would interfere
 * with our own assert redefinition, include this header first. */
#if !defined(__clang__) && defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3))
#	include <debug/debug.h>
#endif

/* Check for mismatching 'architectures' */
#if !defined(STRGEN) && !defined(SETTINGSGEN) && ((defined(__LP64__) && !defined(_SQ64)) || (!defined(__LP64__) && defined(_SQ64)))
#	error "Compiling 64 bits without _SQ64 set! (or vice versa)"
#endif

#define MAC_OS_X_VERSION_MIN_REQUIRED MAC_OS_X_VERSION_10_3
#include <AvailabilityMacros.h>

/* Name conflict */
#define Rect        OTTDRect
#define Point       OTTDPoint
#define WindowClass OTTDWindowClass

#include <CoreServices/CoreServices.h>

#undef Rect
#undef Point
#undef WindowClass

/* remove the variables that CoreServices defines, but we define ourselves too */
#undef bool
#undef false
#undef true

/* Name conflict */
#define GetTime OTTD_GetTime

#define SL_ERROR OSX_SL_ERROR

/* NSInteger and NSUInteger are part of 10.5 and higher. */
#ifndef NSInteger
#ifdef __LP64__
typedef long NSInteger;
typedef unsigned long NSUInteger;
#else
typedef int NSInteger;
typedef unsigned int NSUInteger;
#endif /* __LP64__ */
#endif /* NSInteger */

#ifndef CGFLOAT_DEFINED
#ifdef __LP64__
typedef double CGFloat;
#else
typedef float CGFloat;
#endif /* __LP64__ */
#endif /* CGFLOAT_DEFINED */

/* OS X SDK versions >= 10.5 have a non-const iconv. */
#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_5
#	define HAVE_NON_CONST_ICONV
#endif

#endif /* MACOS_STDAFX_H */
