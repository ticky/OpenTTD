/* $Id$ */

/** @file mixer.h */

#ifndef MIXER_H
#define MIXER_H

struct MixerChannel;

enum {
	MX_AUTOFREE = 1,
//	MX_8BIT = 2,
//	MX_STEREO = 4,
//	MX_UNSIGNED = 8,
};

bool MxInitialize(uint rate);
void MxMixSamples(void *buffer, uint samples);

MixerChannel *MxAllocateChannel();
void MxSetChannelRawSrc(MixerChannel *mc, int8 *mem, uint size, uint rate, uint flags);
void MxSetChannelVolume(MixerChannel *mc, uint left, uint right);
void MxActivateChannel(MixerChannel*);

#endif /* MIXER_H */
