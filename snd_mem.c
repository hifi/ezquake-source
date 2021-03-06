/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

    $Id: snd_mem.c,v 1.16 2007-10-25 14:54:30 dkure Exp $
*/
// snd_mem.c -- sound caching

#include "quakedef.h"
#include "fmod.h"
#include "qsound.h"

#define LINEARUPSCALE(in, inrate, insamps, out, outrate, outlshift, outrshift) \
	{ \
		scale = inrate / (double)outrate; \
		infrac = floor(scale * 65536); \
		outsamps = insamps / scale; \
		inaccum = 0; \
		outnlsamps = floor(1.0 / scale); \
		outsamps -= outnlsamps; \
	\
		while (outsamps) \
		{ \
			*out = ((0xFFFF - inaccum)*in[0] + inaccum*in[1]) >> (16 - outlshift + outrshift); \
			inaccum += infrac; \
			in += (inaccum >> 16); \
			inaccum &= 0xFFFF; \
			out++; \
			outsamps--; \
		} \
		while (outnlsamps) \
		{ \
			*out = (*in >> outrshift) << outlshift; \
			out++; \
			outnlsamps--; \
		} \
	}

#define LINEARUPSCALESTEREO(in, inrate, insamps, out, outrate, outlshift, outrshift) \
	{ \
		scale = inrate / (double)outrate; \
		infrac = floor(scale * 65536); \
		outsamps = insamps / scale; \
		inaccum = 0; \
		outnlsamps = floor(1.0 / scale); \
		outsamps -= outnlsamps; \
	\
		while (outsamps) \
		{ \
			out[0] = ((0xFFFF - inaccum)*in[0] + inaccum*in[2]) >> (16 - outlshift + outrshift); \
			out[1] = ((0xFFFF - inaccum)*in[1] + inaccum*in[3]) >> (16 - outlshift + outrshift); \
			inaccum += infrac; \
			in += (inaccum >> 16) * 2; \
			inaccum &= 0xFFFF; \
			out += 2; \
			outsamps--; \
		} \
		while (outnlsamps) \
		{ \
			out[0] = (in[0] >> outrshift) << outlshift; \
			out[1] = (in[1] >> outrshift) << outlshift; \
			out += 2; \
			outnlsamps--; \
		} \
	}

#define LINEARUPSCALESTEREOTOMONO(in, inrate, insamps, out, outrate, outlshift, outrshift) \
	{ \
		scale = inrate / (double)outrate; \
		infrac = floor(scale * 65536); \
		outsamps = insamps / scale; \
		inaccum = 0; \
		outnlsamps = floor(1.0 / scale); \
		outsamps -= outnlsamps; \
	\
		while (outsamps) \
		{ \
			*out = ((((0xFFFF - inaccum)*in[0] + inaccum*in[2]) >> (16 - outlshift + outrshift)) + \
				(((0xFFFF - inaccum)*in[1] + inaccum*in[3]) >> (16 - outlshift + outrshift))) >> 1; \
			inaccum += infrac; \
			in += (inaccum >> 16) * 2; \
			inaccum &= 0xFFFF; \
			out++; \
			outsamps--; \
		} \
		while (outnlsamps) \
		{ \
			out[0] = (((in[0] >> outrshift) << outlshift) + ((in[1] >> outrshift) << outlshift)) >> 1; \
			out++; \
			outnlsamps--; \
		} \
	}

#define LINEARDOWNSCALE(in, inrate, insamps, out, outrate, outlshift, outrshift) \
	{ \
		scale = outrate / (double)inrate; \
		infrac = floor(scale * 65536); \
		inaccum = 0; \
		insamps--; \
		outsampleft = 0; \
	\
		while (insamps) \
		{ \
			inaccum += infrac; \
			if (inaccum >> 16) \
			{ \
				inaccum &= 0xFFFF; \
				outsampleft += (infrac - inaccum) * (*in); \
				*out = outsampleft >> (16 - outlshift + outrshift); \
				out++; \
				outsampleft = inaccum * (*in); \
			} \
			else \
				outsampleft += infrac * (*in); \
			in++; \
			insamps--; \
		} \
		outsampleft += (0xFFFF - inaccum) * (*in);\
		*out = outsampleft >> (16 - outlshift + outrshift); \
	}

#define LINEARDOWNSCALESTEREO(in, inrate, insamps, out, outrate, outlshift, outrshift) \
	{ \
		scale = outrate / (double)inrate; \
		infrac = floor(scale * 65536); \
		inaccum = 0; \
		insamps--; \
		outsampleft = 0; \
		outsampright = 0; \
	\
		while (insamps) \
		{ \
			inaccum += infrac; \
			if (inaccum >> 16) \
			{ \
				inaccum &= 0xFFFF; \
				outsampleft += (infrac - inaccum) * in[0]; \
				outsampright += (infrac - inaccum) * in[1]; \
				out[0] = outsampleft >> (16 - outlshift + outrshift); \
				out[1] = outsampright >> (16 - outlshift + outrshift); \
				out += 2; \
				outsampleft = inaccum * in[0]; \
				outsampright = inaccum * in[1]; \
			} \
			else \
			{ \
				outsampleft += infrac * in[0]; \
				outsampright += infrac * in[1]; \
			} \
			in += 2; \
			insamps--; \
		} \
		outsampleft += (0xFFFF - inaccum) * in[0];\
		outsampright += (0xFFFF - inaccum) * in[1];\
		out[0] = outsampleft >> (16 - outlshift + outrshift); \
		out[1] = outsampright >> (16 - outlshift + outrshift); \
	}

#define LINEARDOWNSCALESTEREOTOMONO(in, inrate, insamps, out, outrate, outlshift, outrshift) \
	{ \
		scale = outrate / (double)inrate; \
		infrac = floor(scale * 65536); \
		inaccum = 0; \
		insamps--; \
		outsampleft = 0; \
	\
		while (insamps) \
		{ \
			inaccum += infrac; \
			if (inaccum >> 16) \
			{ \
				inaccum &= 0xFFFF; \
				outsampleft += (infrac - inaccum) * ((in[0] + in[1]) >> 1); \
				*out = outsampleft >> (16 - outlshift + outrshift); \
				out++; \
				outsampleft = inaccum * ((in[0] + in[1]) >> 1); \
			} \
			else \
				outsampleft += infrac * ((in[0] + in[1]) >> 1); \
			in += 2; \
			insamps--; \
		} \
		outsampleft += (0xFFFF - inaccum) * ((in[0] + in[1]) >> 1);\
		*out = outsampleft >> (16 - outlshift + outrshift); \
	}

#define STANDARDRESCALE(in, inrate, insamps, out, outrate, outlshift, outrshift) \
	{ \
		scale = inrate / (double)outrate; \
		infrac = floor(scale * 65536); \
		outsamps = insamps / scale; \
		inaccum = 0; \
	\
		while (outsamps) \
		{ \
			*out = (*in >> outrshift) << outlshift; \
			inaccum += infrac; \
			in += (inaccum >> 16); \
			inaccum &= 0xFFFF; \
			out++; \
			outsamps--; \
		} \
	}

#define STANDARDRESCALESTEREO(in, inrate, insamps, out, outrate, outlshift, outrshift) \
	{ \
		scale = inrate / (double)outrate; \
		infrac = floor(scale * 65536); \
		outsamps = insamps / scale; \
		inaccum = 0; \
	\
		while (outsamps) \
		{ \
			out[0] = (in[0] >> outrshift) << outlshift; \
			out[1] = (in[1] >> outrshift) << outlshift; \
			inaccum += infrac; \
			in += (inaccum >> 16) * 2; \
			inaccum &= 0xFFFF; \
			out += 2; \
			outsamps--; \
		} \
	}

#define STANDARDRESCALESTEREOTOMONO(in, inrate, insamps, out, outrate, outlshift, outrshift) \
	{ \
		scale = inrate / (double)outrate; \
		infrac = floor(scale * 65536); \
		outsamps = insamps / scale; \
		inaccum = 0; \
	\
		while (outsamps) \
		{ \
			out[0] = (((in[0] >> outrshift) << outlshift) + ((in[1] >> outrshift) << outlshift)) >> 1; \
			inaccum += infrac; \
			in += (inaccum >> 16) * 2; \
			inaccum &= 0xFFFF; \
			out++; \
			outsamps--; \
		} \
	}

#define QUICKCONVERT(in, insamps, out, outlshift, outrshift) \
	{ \
		while (insamps) \
		{ \
			*out = (*in >> outrshift) << outlshift; \
			out++; \
			in++; \
			insamps--; \
		} \
	}

#define QUICKCONVERTSTEREOTOMONO(in, insamps, out, outlshift, outrshift) \
	{ \
		while (insamps) \
		{ \
			*out = (((in[0] >> outrshift) << outlshift) + ((in[1] >> outrshift) << outlshift)) >> 1; \
			out++; \
			in += 2; \
			insamps--; \
		} \
	}

// SND_ResampleStream: takes a sound stream and converts with given parameters. Limited to
// 8-16-bit signed conversions and mono-to-mono/stereo-to-stereo conversions.
// Not an in-place algorithm.
void SND_ResampleStream (void *in, int inrate, int inwidth, int inchannels, int insamps, void *out, int outrate, int outwidth, int outchannels, int resampstyle)
{
	double scale;
	signed char *in8 = (signed char *)in;
	short *in16 = (short *)in;
	signed char *out8 = (signed char *)out;
	short *out16 = (short *)out;
	int outsamps, outnlsamps, outsampleft, outsampright;
	int infrac, inaccum;

	if (insamps <= 0)
		return;

	if (inchannels == outchannels && inwidth == outwidth && inrate == outrate)
	{
		memcpy(out, in, inwidth*insamps*inchannels);
		return;
	}

	if (inchannels == 1 && outchannels == 1)
	{
		if (inwidth == 1)
		{
			if (outwidth == 1)
			{
				if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALE(in8, inrate, insamps, out8, outrate, 0, 0)
					else
						STANDARDRESCALE(in8, inrate, insamps, out8, outrate, 0, 0)
				}
				else // downsample
				{
					if (resampstyle > 1)
						LINEARDOWNSCALE(in8, inrate, insamps, out8, outrate, 0, 0)
					else
						STANDARDRESCALE(in8, inrate, insamps, out8, outrate, 0, 0)
				}
				return;
			}
			else
			{
				if (inrate == outrate) // quick convert
					QUICKCONVERT(in8, insamps, out16, 8, 0)
				else if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALE(in8, inrate, insamps, out16, outrate, 8, 0)
					else
						STANDARDRESCALE(in8, inrate, insamps, out16, outrate, 8, 0)
				}
				else // downsample
				{
					if (resampstyle > 1)
						LINEARDOWNSCALE(in8, inrate, insamps, out16, outrate, 8, 0)
					else
						STANDARDRESCALE(in8, inrate, insamps, out16, outrate, 8, 0)
				}
				return;
			}
		}
		else // 16-bit
		{
			if (outwidth == 2)
			{
				if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALE(in16, inrate, insamps, out16, outrate, 0, 0)
					else
						STANDARDRESCALE(in16, inrate, insamps, out16, outrate, 0, 0)
				}
				else // downsample
				{
					if (resampstyle > 1)
						LINEARDOWNSCALE(in16, inrate, insamps, out16, outrate, 0, 0)
					else
						STANDARDRESCALE(in16, inrate, insamps, out16, outrate, 0, 0)
				}
				return;
			}
			else
			{
				if (inrate == outrate) // quick convert
					QUICKCONVERT(in16, insamps, out8, 0, 8)
				else if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALE(in16, inrate, insamps, out8, outrate, 0, 8)
					else
						STANDARDRESCALE(in16, inrate, insamps, out8, outrate, 0, 8)
				}
				else // downsample
				{
					if (resampstyle > 1)
						LINEARDOWNSCALE(in16, inrate, insamps, out8, outrate, 0, 8)
					else
						STANDARDRESCALE(in16, inrate, insamps, out8, outrate, 0, 8)
				}
				return;
			}
		}
	}
	else if (outchannels == 2 && inchannels == 2)
	{
		if (inwidth == 1)
		{
			if (outwidth == 1)
			{
				if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALESTEREO(in8, inrate, insamps, out8, outrate, 0, 0)
					else
						STANDARDRESCALESTEREO(in8, inrate, insamps, out8, outrate, 0, 0)
				}
				else // downsample
				{
					if (resampstyle > 1)
						LINEARDOWNSCALESTEREO(in8, inrate, insamps, out8, outrate, 0, 0)
					else
						STANDARDRESCALESTEREO(in8, inrate, insamps, out8, outrate, 0, 0)
				}
			}
			else
			{
				if (inrate == outrate) // quick convert
				{
					insamps *= 2;
					QUICKCONVERT(in8, insamps, out16, 8, 0)
				}
				else if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALESTEREO(in8, inrate, insamps, out16, outrate, 8, 0)
					else
						STANDARDRESCALESTEREO(in8, inrate, insamps, out16, outrate, 8, 0)
				}
				else // downsample
				{
					if (resampstyle > 1)
						LINEARDOWNSCALESTEREO(in8, inrate, insamps, out16, outrate, 8, 0)
					else
						STANDARDRESCALESTEREO(in8, inrate, insamps, out16, outrate, 8, 0)
				}
			}
		}
		else // 16-bit
		{
			if (outwidth == 2)
			{
				if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALESTEREO(in16, inrate, insamps, out16, outrate, 0, 0)
					else
						STANDARDRESCALESTEREO(in16, inrate, insamps, out16, outrate, 0, 0)
				}
				else // downsample
				{
					if (resampstyle > 1)
						LINEARDOWNSCALESTEREO(in16, inrate, insamps, out16, outrate, 0, 0)
					else
						STANDARDRESCALESTEREO(in16, inrate, insamps, out16, outrate, 0, 0)
				}
			}
			else 
			{
				if (inrate == outrate) // quick convert
				{
					insamps *= 2;
					QUICKCONVERT(in16, insamps, out8, 0, 8)
				}
				else if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALESTEREO(in16, inrate, insamps, out8, outrate, 0, 8)
					else
						STANDARDRESCALESTEREO(in16, inrate, insamps, out8, outrate, 0, 8)
				}
				else // downsample
				{
					if (resampstyle > 1)
						LINEARDOWNSCALESTEREO(in16, inrate, insamps, out8, outrate, 0, 8)
					else
						STANDARDRESCALESTEREO(in16, inrate, insamps, out8, outrate, 0, 8)
				}
			}
		}
	}
#if 0
	else if (outchannels == 1 && inchannels == 2)
	{
		if (inwidth == 1)
		{
			if (outwidth == 1)
			{
				if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALESTEREOTOMONO(in8, inrate, insamps, out8, outrate, 0, 0)
					else
						STANDARDRESCALESTEREOTOMONO(in8, inrate, insamps, out8, outrate, 0, 0)
				}
				else // downsample
					STANDARDRESCALESTEREOTOMONO(in8, inrate, insamps, out8, outrate, 0, 0)
			}
			else
			{
				if (inrate == outrate) // quick convert
					QUICKCONVERTSTEREOTOMONO(in8, insamps, out16, 8, 0)
				else if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALESTEREOTOMONO(in8, inrate, insamps, out16, outrate, 8, 0)
					else
						STANDARDRESCALESTEREOTOMONO(in8, inrate, insamps, out16, outrate, 8, 0)
				}
				else // downsample
					STANDARDRESCALESTEREOTOMONO(in8, inrate, insamps, out16, outrate, 8, 0)
			}
		}
		else // 16-bit
		{
			if (outwidth == 2)
			{
				if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALESTEREOTOMONO(in16, inrate, insamps, out16, outrate, 0, 0)
					else
						STANDARDRESCALESTEREOTOMONO(in16, inrate, insamps, out16, outrate, 0, 0)
				}
				else // downsample
					STANDARDRESCALESTEREOTOMONO(in16, inrate, insamps, out16, outrate, 0, 0)
			}
			else 
			{
				if (inrate == outrate) // quick convert
					QUICKCONVERTSTEREOTOMONO(in16, insamps, out8, 0, 8)
				else if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALESTEREOTOMONO(in16, inrate, insamps, out8, outrate, 0, 8)
					else
						STANDARDRESCALESTEREOTOMONO(in16, inrate, insamps, out8, outrate, 0, 8)
				}
				else // downsample
					STANDARDRESCALESTEREOTOMONO(in16, inrate, insamps, out8, outrate, 0, 8)
			}
		}
	}
#endif
}

/*
================
ResampleSfx
================
*/
void ResampleSfx (sfx_t *sfx, int inrate, int inchannels, int inwidth, int insamps, int inloopstart, byte *data)
{
	extern cvar_t s_linearresample;
	double scale;
	sfxcache_t	*sc;
	int len;
	int outsamps;
	int outwidth;
	int outchannels = 1; // inchannels;

	scale = shm->format.speed / (double)inrate;
	outsamps = insamps * scale;
	if (s_loadas8bit.integer < 0)
		outwidth = 2;
	else if (s_loadas8bit.integer)
		outwidth = 1;
	else
		outwidth = inwidth;
	len = outsamps * outwidth * outchannels;

	sc = Cache_Alloc (&sfx->cache, len + sizeof(sfxcache_t), sfx->name);
	if (!sc)
	{
		return;
	}

	sc->format.channels = outchannels;
	sc->format.width = outwidth;
	sc->format.speed = shm->format.speed;
	sc->total_length = outsamps;
	if (inloopstart == -1)
		sc->loopstart = inloopstart;
	else
		sc->loopstart = inloopstart * scale;

	SND_ResampleStream (data, 
		inrate, 
		inwidth, 
		inchannels, 
		insamps, 
		sc->data, 
		sc->format.speed, 
		sc->format.width, 
		sc->format.channels, 
		s_linearresample.integer);
}

/*
===============================================================================
WAV loading
===============================================================================
*/

static unsigned char *data_p;
static unsigned char *iff_end;
static unsigned char *last_chunk;
static unsigned char *iff_data;
static int iff_chunk_len;

static short GetLittleShort(void)
{
	short val;

	val = BuffLittleShort (data_p);
	data_p += 2;

	return val;
}

static int GetLittleLong(void)
{
	int val = 0;

	val = BuffLittleLong (data_p);
	data_p += 4;

	return val;
}

static void FindNextChunk(char *name)
{
	while (1) {
		data_p=last_chunk;

		if (data_p >= iff_end) { // didn't find the chunk
			data_p = NULL;
			return;
		}

		data_p += 4;
		iff_chunk_len = GetLittleLong();
		if (iff_chunk_len < 0) {
			data_p = NULL;
			return;
		}

		data_p -= 8;
		last_chunk = data_p + 8 + ( (iff_chunk_len + 1) & ~1 );
		if (!strncmp((const char *)data_p, name, 4))
			return;
	}
}

static void FindChunk(char *name)
{
	last_chunk = iff_data;
	FindNextChunk (name);
}

static wavinfo_t GetWavinfo (char *name, unsigned char *wav, int wavlength)
{
	int samples, format, i;
	wavinfo_t info;

	memset (&info, 0, sizeof(info));

	if (!wav)
		return info;

	iff_data = wav;
	iff_end = wav + wavlength;

	// find "RIFF" chunk
	FindChunk("RIFF");
	if (!(data_p && !strncmp((const char *)(data_p+8), "WAVE", 4))) {
		Com_Printf ("Missing RIFF/WAVE chunks\n");
		return info;
	}

	// get "fmt " chunk
	iff_data = data_p + 12;
	FindChunk("fmt ");
	if (!data_p) {
		Com_Printf ("Missing fmt chunk\n");
		return info;
	}

	data_p += 8;
	format = GetLittleShort();
	if (format != 1) {
		Com_Printf ("Microsoft PCM format only\n");
		return info;
	}

	info.channels = GetLittleShort();
	info.rate = GetLittleLong();
	data_p += 4 + 2;
	info.width = GetLittleShort() / 8;

	// get cue chunk
	FindChunk("cue ");
	if (data_p) {
		data_p += 32;
		info.loopstart = GetLittleLong();

		// if the next chunk is a LIST chunk, look for a cue length marker
		FindNextChunk ("LIST");
		if (data_p) {
			// this is not a proper parse, but it works with cooledit...
			if (!strncmp ((const char *)(data_p + 28), "mark", 4)) {
				data_p += 24;
				i = GetLittleLong (); // samples in loop
				info.samples = info.loopstart + i;
			}
		}
	} else
		info.loopstart = -1;

	// find data chunk
	FindChunk("data");
	if (!data_p) {
		Com_Printf ("Missing data chunk\n");
		return info;
	}

	data_p += 4;
	samples = GetLittleLong () / info.width / info.channels;

	if (info.samples) {
		if (samples < info.samples)
			Sys_Error ("Sound %s has a bad loop length", name);
	} else
		info.samples = samples;

	info.dataofs = data_p - wav;

	return info;
}


//=============================================================================

static void COM_SwapLittleShortBlock (short *s, int size)
{
//FIXME: qqshka: I have no idea that we have to do for PDP endian.

#if defined __BIG_ENDIAN__
//	if (!bigendian)
//		return;

	if (size <= 0)
		return;

	while (size)
	{
		*s = ShortSwap(*s);
		s++;
		size--;
	}
#endif
}

static void COM_CharBias (signed char *c, int size)
{
	if (size <= 0)
		return;

	while (size)
	{
		*c = (*(unsigned char *)c) - 128;
		c++;
		size--;
	}
}

#ifndef WITH_OGG_VORBIS
sfxcache_t *S_LoadSound (sfx_t *s)
{
	char namebuffer[256];
	unsigned char *data;
	sfxcache_t *sc;
	wavinfo_t info;
	int filesize;

	// see if still in memory
	if ((sc = (sfxcache_t *) Cache_Check (&s->cache)))
		return sc;

	// load it in
	snprintf (namebuffer, sizeof (namebuffer), "sound/%s", s->name);

	if (!(data = FS_LoadTempFile (namebuffer, &filesize))) {
		Com_Printf ("Couldn't load %s\n", namebuffer);
		return NULL;
	}

	FMod_CheckModel(namebuffer, data, filesize);

	info = GetWavinfo (s->name, data, filesize);

	// Stereo sounds are allowed (intended for music)
	if (info.channels < 1 || info.channels > 2) {
		Com_Printf("%s has an unsupported number of channels (%i)\n",s->name, info.channels);
		return NULL;
	}

	if (info.width == 1)
		COM_CharBias((signed char*)data + info.dataofs, info.samples * info.channels);
	else if (info.width == 2)
		COM_SwapLittleShortBlock((short *)(data + info.dataofs), info.samples * info.channels);

	ResampleSfx (s, info.rate, info.channels, info.width, info.samples, info.loopstart, data + info.dataofs);

	return Cache_Check(&s->cache);
}
#endif // WITH_OGG_VORBIS

int SND_Rate(int rate)
{
	switch (rate)
	{
		case 48:
			return 48000;
		case 44:
			return 44100;
		case 32:
			return 32000;
		case 24:
			return 24000;
		case 22:
			return 22050;
		case 16:
			return 16000;
		case 12:
			return 12000;
		case 8:
			return 8000;
		default:
			return 11025;
	}
}
