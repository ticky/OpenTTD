/* $Id$ */

/** @file music.h Base for the music handling. */

#ifndef MUSIC_H
#define MUSIC_H

#define NUM_SONGS_PLAYLIST 33
#define NUM_SONGS_AVAILABLE 22

struct SongSpecs {
	char filename[256];
	char song_name[64];
};

extern const SongSpecs origin_songs_specs[NUM_SONGS_AVAILABLE];

#endif //MUSIC_H
