/* Metadata extraction
 *
 * Project : minidlna
 * Website : http://sourceforge.net/projects/minidlna/
 * Author  : Justin Maggard
 *
 * MiniDLNA media server
 * Copyright (C) 2008-2009  Justin Maggard
 *
 * This file is part of MiniDLNA.
 *
 * MiniDLNA is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * MiniDLNA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MiniDLNA. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __METADATA_H__
#define __METADATA_H__

#define FLAG_TITLE	        0x00000001
#define FLAG_ARTIST	        0x00000002
#define FLAG_ALBUM	        0x00000004
#define FLAG_GENRE	        0x00000008
#define FLAG_COMMENT	        0x00000010
#define FLAG_CREATOR	        0x00000020
#define FLAG_DATE	        0x00000040
#define FLAG_DLNA_PN	        0x00000080
#define FLAG_MIME	        0x00000100
#define FLAG_DURATION	        0x00000200
#define FLAG_RESOLUTION	        0x00000400
#define FLAG_DESCRIPTION        0x00000800
#define FLAG_RATING             0x00001000
#define FLAG_AUTHOR             0x00002000
#define FLAG_TRACK              0x00004000
#define FLAG_DISC               0x00008000
#define FLAG_PUBLISHER          0x00010000
#define FLAG_DIRECTOR           0x00020000
#define FLAG_ORIG_COMMENT       0x00040000
#define FLAG_ORIG_DESCRIPTION   0x00080000
#define FLAG_ORIG_DATE          0x00100000
#define FLAG_ORIG_RATING        0x00200000
#define FLAG_ORIG_TRACK         0x00400000
#define FLAG_ORIG_DISC          0x00800000
#define FLAG_ORIG_CREATOR       0x01000000
#define FLAG_ORIG_AUTHOR        0x02000000

#define ALL_FLAGS               0xFFFFFFFF

#define FLAG_ART_TYPE_POSTER    0x01
#define FLAG_ART_TYPE_BACKDROP  0x02
#define FLAG_ART_TYPE_STILL     0x04
#define FLAG_ART_TYPE_ANY       FLAG_ART_TYPE_POSTER|FLAG_ART_TYPE_BACKDROP|FLAG_ART_TYPE_STILL

typedef struct artwork_s {
	uint8_t  thumb_type;
	uint8_t *thumb_data;
	size_t   thumb_size;
	struct artwork_s *next;
} artwork_t;

typedef struct metadata_s {
	char *       title;
	char *       artist;
	char *       creator;
	char *       publisher;
	char *       author;
	char *       album;
	char *       genre;
	char *       comment;
	char *       description;
	char *       rating;
	unsigned int disc;
	unsigned int track;
	unsigned int channels;
	unsigned int bitrate;
	unsigned int frequency;
	unsigned int rotation;
	char *       resolution;
	char *       duration;
	char *       date;
	char *       mime;
	char *       dlna_pn;
	char *       director;
	char *       original_comment;
	char *       original_description;
	char *       original_date;
	char *       original_rating;
	char *       original_author;
	char *       original_creator;
	unsigned int original_disc;
	unsigned int original_track;
	artwork_t *  artwork;
} metadata_t;

typedef enum {
  AAC_INVALID   =  0,
  AAC_MAIN      =  1, /* AAC Main */
  AAC_LC        =  2, /* AAC Low complexity */
  AAC_SSR       =  3, /* AAC SSR */
  AAC_LTP       =  4, /* AAC Long term prediction */
  AAC_HE        =  5, /* AAC High efficiency (SBR) */
  AAC_SCALE     =  6, /* Scalable */
  AAC_TWINVQ    =  7, /* TwinVQ */
  AAC_CELP      =  8, /* CELP */
  AAC_HVXC      =  9, /* HVXC */
  AAC_TTSI      = 12, /* TTSI */
  AAC_MS        = 13, /* Main synthetic */
  AAC_WAVE      = 14, /* Wavetable synthesis */
  AAC_MIDI      = 15, /* General MIDI */
  AAC_FX        = 16, /* Algorithmic Synthesis and Audio FX */
  AAC_LC_ER     = 17, /* AAC Low complexity with error recovery */
  AAC_LTP_ER    = 19, /* AAC Long term prediction with error recovery */
  AAC_SCALE_ER  = 20, /* AAC scalable with error recovery */
  AAC_TWINVQ_ER = 21, /* TwinVQ with error recovery */
  AAC_BSAC_ER   = 22, /* BSAC with error recovery */
  AAC_LD_ER     = 23, /* AAC LD with error recovery */
  AAC_CELP_ER   = 24, /* CELP with error recovery */
  AAC_HXVC_ER   = 25, /* HXVC with error recovery */
  AAC_HILN_ER   = 26, /* HILN with error recovery */
  AAC_PARAM_ER  = 27, /* Parametric with error recovery */
  AAC_SSC       = 28, /* AAC SSC */
  AAC_HE_L3     = 31, /* Reserved : seems to be HeAAC L3 */
} aac_object_type_t;

typedef enum {
	NONE,
	EMPTY,
	VALID
} ts_timestamp_t;

int
ends_with(const char *haystack, const char *needle);

void
check_for_captions(const char *path, int64_t detailID);

int64_t
GetFolderMetadata(const char *name, const char *path, const char *artist, const char *genre, int64_t album_art);

int64_t
GetAudioMetadata(const char *path, char *name);

int64_t
GetImageMetadata(const char *path, char *name);

int64_t
GetVideoMetadata(const char *path, char *name);

void
free_metadata(metadata_t *m, uint32_t flags);

void
free_metadata_artwork(metadata_t *m);
#endif
