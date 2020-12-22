/* $Id$ */

/** @file 32bpp_base.cpp Implementation of base for 32 bpp blitters. */

#include "../stdafx.h"
#include "../gfx_func.h"
#include "32bpp_base.hpp"

#include "../safeguards.h"

void *Blitter_32bppBase::MoveTo(const void *video, int x, int y)
{
	return (uint32 *)video + x + y * _screen.pitch;
}

void Blitter_32bppBase::SetPixel(void *video, int x, int y, uint8 color)
{
	*((uint32 *)video + x + y * _screen.pitch) = LookupColourInPalette(color);
}

void Blitter_32bppBase::DrawRect(void *video, int width, int height, uint8 color)
{
	uint32 color32 = LookupColourInPalette(color);

	do {
		uint32 *dst = (uint32 *)video;
		for (int i = width; i > 0; i--) {
			*dst = color32;
			dst++;
		}
		video = (uint32 *)video + _screen.pitch;
	} while (--height);
}

void Blitter_32bppBase::CopyFromBuffer(void *video, const void *src, int width, int height)
{
	uint32 *dst = (uint32 *)video;
	uint32 *usrc = (uint32 *)src;

	for (; height > 0; height--) {
		memcpy(dst, usrc, width * sizeof(uint32));
		usrc += width;
		dst += _screen.pitch;
	}
}

void Blitter_32bppBase::CopyToBuffer(const void *video, void *dst, int width, int height)
{
	uint32 *udst = (uint32 *)dst;
	uint32 *src = (uint32 *)video;

	for (; height > 0; height--) {
		memcpy(udst, src, width * sizeof(uint32));
		src += _screen.pitch;
		udst += width;
	}
}

void Blitter_32bppBase::CopyImageToBuffer(const void *video, void *dst, int width, int height, int dst_pitch)
{
	uint32 *udst = (uint32 *)dst;
	uint32 *src = (uint32 *)video;

	for (; height > 0; height--) {
		memcpy(udst, src, width * sizeof(uint32));
		src += _screen.pitch;
		udst += dst_pitch;
	}
}

void Blitter_32bppBase::ScrollBuffer(void *video, int &left, int &top, int &width, int &height, int scroll_x, int scroll_y)
{
	const uint32 *src;
	uint32 *dst;

	if (scroll_y > 0) {
		/*Calculate pointers */
		dst = (uint32 *)video + left + (top + height - 1) * _screen.pitch;
		src = dst - scroll_y * _screen.pitch;

		/* Decrease height and increase top */
		top += scroll_y;
		height -= scroll_y;
		assert(height > 0);

		/* Adjust left & width */
		if (scroll_x >= 0) {
			dst += scroll_x;
			left += scroll_x;
			width -= scroll_x;
		} else {
			src -= scroll_x;
			width += scroll_x;
		}

		for (int h = height; h > 0; h--) {
			memcpy(dst, src, width * sizeof(uint32));
			src -= _screen.pitch;
			dst -= _screen.pitch;
		}
	} else {
		/* Calculate pointers */
		dst = (uint32 *)video + left + top * _screen.pitch;
		src = dst - scroll_y * _screen.pitch;

		/* Decrese height. (scroll_y is <=0). */
		height += scroll_y;
		assert(height > 0);

		/* Adjust left & width */
		if (scroll_x >= 0) {
			dst += scroll_x;
			left += scroll_x;
			width -= scroll_x;
		} else {
			src -= scroll_x;
			width += scroll_x;
		}

		/* the y-displacement may be 0 therefore we have to use memmove,
		 * because source and destination may overlap */
		for (int h = height; h > 0; h--) {
			memmove(dst, src, width * sizeof(uint32));
			src += _screen.pitch;
			dst += _screen.pitch;
		}
	}
}

int Blitter_32bppBase::BufferSize(int width, int height)
{
	return width * height * sizeof(uint32);
}

void Blitter_32bppBase::PaletteAnimate(uint start, uint count)
{
	/* By default, 32bpp doesn't have palette animation */
}

Blitter::PaletteAnimation Blitter_32bppBase::UsePaletteAnimation()
{
	return Blitter::PALETTE_ANIMATION_NONE;
}
