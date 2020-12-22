/* $Id$ */

/** @file 32bpp_optimized.hpp Optimized 32 bpp blitter. */

#ifndef BLITTER_32BPP_OPTIMIZED_HPP
#define BLITTER_32BPP_OPTIMIZED_HPP

#include "32bpp_simple.hpp"
#include "factory.hpp"

/** The optimised 32 bpp blitter (without palette animation). */
class Blitter_32bppOptimized : public Blitter_32bppSimple {
public:
	/** Data stored about a (single) sprite. */
	struct SpriteData {
		uint32 offset[ZOOM_LVL_COUNT][2]; ///< Offsets (from .data) to streams for different zoom levels, and the normal and remap image information.
		byte data[VARARRAY_SIZE];         ///< Data, all zoomlevels.
	};

	/* virtual */ void Draw(Blitter::BlitterParams *bp, BlitterMode mode, ZoomLevel zoom);
	/* virtual */ Sprite *Encode(SpriteLoader::Sprite *sprite, AllocatorProc *allocator);

	/* virtual */ const char *GetName() { return "32bpp-optimized"; }

	template <BlitterMode mode> void Draw(const Blitter::BlitterParams *bp, ZoomLevel zoom);
};

/** Factory for the optimised 32 bpp blitter (without palette animation). */
class FBlitter_32bppOptimized: public BlitterFactory<FBlitter_32bppOptimized> {
public:
	/* virtual */ const char *GetName() { return "32bpp-optimized"; }
	/* virtual */ const char *GetDescription() { return "32bpp Optimized Blitter (no palette animation)"; }
	/* virtual */ Blitter *CreateInstance() { return new Blitter_32bppOptimized(); }
};

#endif /* BLITTER_32BPP_OPTIMIZED_HPP */
