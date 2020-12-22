/* $Id$ */

/** @file 8bpp_optimized.hpp An optimized 8 bpp blitter. */

#ifndef BLITTER_8BPP_OPTIMIZED_HPP
#define BLITTER_8BPP_OPTIMIZED_HPP

#include "8bpp_base.hpp"
#include "factory.hpp"

/** 8bpp blitter optimised for speed. */
class Blitter_8bppOptimized : public Blitter_8bppBase {
public:
	/** Data stored about a (single) sprite. */
	struct SpriteData {
		uint32 offset[ZOOM_LVL_COUNT]; ///< Offsets (from .data) to streams for different zoom levels.
		byte data[VARARRAY_SIZE];      ///< Data, all zoomlevels.
	};

	/* virtual */ void Draw(Blitter::BlitterParams *bp, BlitterMode mode, ZoomLevel zoom);
	/* virtual */ Sprite *Encode(SpriteLoader::Sprite *sprite, AllocatorProc *allocator);

	/* virtual */ const char *GetName() { return "8bpp-optimized"; }
};

/** Factory for the 8bpp blitter optimised for speed. */
class FBlitter_8bppOptimized: public BlitterFactory<FBlitter_8bppOptimized> {
public:
	/* virtual */ const char *GetName() { return "8bpp-optimized"; }
	/* virtual */ const char *GetDescription() { return "8bpp Optimized Blitter (compression + all-ZoomLevel cache)"; }
	/* virtual */ Blitter *CreateInstance() { return new Blitter_8bppOptimized(); }
};

#endif /* BLITTER_8BPP_OPTIMIZED_HPP */
