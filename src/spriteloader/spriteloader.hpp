/* $Id$ */

/** @file spriteloader.hpp Base for loading sprites. */

#ifndef SPRITELOADER_HPP
#define SPRITELOADER_HPP

class SpriteLoader {
public:
	struct CommonPixel {
		uint8 r;  ///< Red-channel
		uint8 g;  ///< Green-channel
		uint8 b;  ///< Blue-channel
		uint8 a;  ///< Alpha-channel
		uint8 m;  ///< Remap-channel
	};

	struct Sprite {
		uint16 height;                   ///< Height of the sprite
		uint16 width;                    ///< Width of the sprite
		int16 x_offs;                    ///< The x-offset of where the sprite will be drawn
		int16 y_offs;                    ///< The y-offset of where the sprite will be drawn
		SpriteLoader::CommonPixel *data; ///< The sprite itself
	};

	/**
	 * Load a sprite from the disk and return a sprite struct which is the same for all loaders.
	 */
	virtual bool LoadSprite(SpriteLoader::Sprite *sprite, uint8 file_slot, uint32 file_pos, SpriteType sprite_type) = 0;

	virtual ~SpriteLoader() { }
};

#endif /* SPRITELOADER_HPP */
