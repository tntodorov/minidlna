/* MiniDLNA media server
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
#include "config.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>

#include "minidlnatypes.h"
#include "upnpglobalvars.h"
#include "utils.h"
#include "log.h"

int
xasprintf(char **strp, char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = vasprintf(strp, fmt, args);
	va_end(args);
	if( ret < 0 )
	{
		DPRINTF(E_WARN, L_GENERAL, "xasprintf: allocation failed\n");
		*strp = NULL;
	}
	return ret;
}

/* Copies characters from one buffer to another; size is maximum size of dst buffer */
void
x_strlcpy(char *dst, const char *src, size_t size)
{
	while (--size > 0 && *src != '\0') {
		*dst++ = *src++;
	}
	*dst = '\0';
}

/* Appends characters from one buffer to another; size is maximum size of dst buffer */
void
x_strlcat(char *dst, const char *src, size_t size)
{
	while (size > 0 && *dst != '\0') {
		size--;
		dst++;
	}

	x_strlcpy(dst, src, size);
}

int
ends_with(const char * haystack, const char * needle)
{
	const char * end;
	int nlen = strlen(needle);
	int hlen = strlen(haystack);

	if( nlen > hlen )
		return 0;
 	end = haystack + hlen - nlen;

	return (strcasecmp(end, needle) ? 0 : 1);
}

char *
trim(char *str)
{
	int i;
	int len;

	if (!str)
		return(NULL);

	len = strlen(str);
	for (i=len-1; i >= 0 && isspace(str[i]); i--)
	{
		str[i] = '\0';
		len--;
	}
	while (isspace(*str))
	{
		str++;
		len--;
	}

	if (str[0] == '"' && str[len-1] == '"')
	{
		str[0] = '\0';
		str[len-1] = '\0';
		str++;
	}

	return str;
}

/* Find the first occurrence of p in s, where s is terminated by t */
char *
strstrc(const char *s, const char *p, const char t)
{
	char *endptr;
	size_t slen, plen;

	endptr = strchr(s, t);
	if (!endptr)
		return strstr(s, p);

	plen = strlen(p);
	slen = endptr - s;
	while (slen >= plen)
	{
		if (*s == *p && strncmp(s+1, p+1, plen-1) == 0)
			return (char*)s;
		s++;
		slen--;
	}

	return NULL;
} 

char *
strcasestrc(const char *s, const char *p, const char t)
{
	char *endptr;
	size_t slen, plen;

	endptr = strchr(s, t);
	if (!endptr)
		return strcasestr(s, p);

	plen = strlen(p);
	slen = endptr - s;
	while (slen >= plen)
	{
		if (*s == *p && strncasecmp(s+1, p+1, plen-1) == 0)
			return (char*)s;
		s++;
		slen--;
	}

	return NULL;
} 

char *
modifyString(char *string, const char *before, const char *after, int noalloc)
{
	int oldlen, newlen, chgcnt = 0;
	char *s, *p;

	/* If there is no match, just return */
	s = strstr(string, before);
	if (!s)
		return string;

	oldlen = strlen(before);
	newlen = strlen(after);
	if (newlen > oldlen)
	{
		if (noalloc)
			return string;

		while ((p = strstr(s, before)))
		{
			chgcnt++;
			s = p + oldlen;
		}
		s = realloc(string, strlen(string)+((newlen-oldlen)*chgcnt)+1);
		/* If we failed to realloc, return the original alloc'd string */
		if( s )
			string = s;
		else
			return string;
	}

	s = string;
	while (s)
	{
		p = strstr(s, before);
		if (!p)
			return string;
		memmove(p + newlen, p + oldlen, strlen(p + oldlen) + 1);
		memcpy(p, after, newlen);
		s = p + newlen;
	}

	return string;
}

char *
unescape_tag(const char *tag, int force_alloc)
{
	char *esc_tag = NULL;

	if (strchr(tag, '&') &&
	    (strstr(tag, "&amp;") || strstr(tag, "&lt;") || strstr(tag, "&gt;") ||
	     strstr(tag, "&quot;") || strstr(tag, "&apos;")))
	{
		esc_tag = strdup(tag);
		esc_tag = modifyString(esc_tag, "&amp;", "&", 1);
		esc_tag = modifyString(esc_tag, "&lt;", "<", 1);
		esc_tag = modifyString(esc_tag, "&gt;", ">", 1);
		esc_tag = modifyString(esc_tag, "&quot;", "\"", 1);
		esc_tag = modifyString(esc_tag, "&apos;", "'", 1);
	}
	else if( force_alloc )
		esc_tag = strdup(tag);

	return esc_tag;
}

char *
escape_tag(const char *tag, int force_alloc)
{
	char *esc_tag = NULL;

	if( strchr(tag, '&') || strchr(tag, '<') || strchr(tag, '>') || strchr(tag, '"') )
	{
		esc_tag = strdup(tag);
		esc_tag = modifyString(esc_tag, "&", "&amp;amp;", 0);
		esc_tag = modifyString(esc_tag, "<", "&amp;lt;", 0);
		esc_tag = modifyString(esc_tag, ">", "&amp;gt;", 0);
		esc_tag = modifyString(esc_tag, "\"", "&amp;quot;", 0);
	}
	else if( force_alloc )
		esc_tag = strdup(tag);

	return esc_tag;
}

char *
strip_ext(char *name)
{
	char *period;

	period = strrchr(name, '.');
	if (period)
		*period = '\0';

	return period;
}

char *
cleanup_name(char *name, const char *strip)
{
	char *clean;
	char *found;
	struct strip_word_s *str;
	size_t len = 0;
	char rem[2] = { 0, 0 };

	if (strip == NULL || (len = strlen(strip)) == 0)
		return trim(name);

	clean = strdup(name);
	for (size_t pos = 0; pos < len; pos++)
	{
		rem[0] = strip[pos];
		clean = modifyString(clean, rem, " ", 0);
	}

	for (str = strip_from_names; str != NULL; str = str->next)
	{
		found = strstr(clean, str->word);
		if (found) *found = '\0';
	}

	return trim(clean);
}

char *
strip_year(char *name)
{
	char *idx, year[5] = { 0 };
	int counter = 0, starts_at = -1, pos = -1;
	idx = name;
	while (*name != '\0')
	{
		pos++;
		if (isdigit(*name))
		{
			if (counter == 0)
				memset(year, 0, sizeof(year));
			if (starts_at == -1)
				starts_at = pos;
			year[counter++] = *name;
			if (counter > 4)
			{
				// obviously not a year
				while (*name != '\0' && isdigit(*name))
				{
					name++;
					pos++;
				}
				counter = 0;
				starts_at = -1;
			}
		}
		else
		{
			if (counter < 4)
				counter = 0;
		}
		name++;
	}

	if ((counter == 4) && (starts_at > 0))
	{
		/* we found a valid year, duplicate it and cut off from it to the end of the name */
		char *ret = strdup(year);
		idx[starts_at] = '\0';
		trim(idx);
		return ret;
	}

	return NULL;
}

enum char_type_e
{
	CHARACTER, DIGIT,
};

void
split_tv_name(const char *input, char **show_name, char **year, char **season, char **episode)
{
	char *work = NULL;
	int found_season = 0, found_episode = 0, found_year = 0;
	size_t season_pos = 0, episode_pos = 0, year_pos = 0;
	char tmp[10] = { 0 };
	uint8_t digit_count = 0;
	size_t total_count = 0;
	enum char_type_e previous = CHARACTER;

	if (input == NULL || strlen(input) == 0)
		return;

	work = strdup(input);
	size_t total_len = strlen(work);

	//DEBUG DPRINTF(E_DEBUG, L_GENERAL, "Workpiece: [%lu] %s\n", total_len, work);

	/* try and isolate the season and episode first */
	while (total_count < total_len)
	{
		if (isdigit(work[total_count]))
		{
			tmp[digit_count++] = work[total_count];
			previous = DIGIT;
			if (digit_count == 10)
				goto clean_up;
		}
		else
		{
			if (previous == DIGIT)
			{
				//DEBUG DPRINTF(E_DEBUG, L_GENERAL, "Digits in buffer: [%d at %d] %s\n", digit_count, total_count, tmp);
				if (digit_count == 4) // may be year or dual-digit season+episode without delimiter or single digit season+3digit episode
				{
					if (!found_year)
					{
						int year_num = atoi(tmp);
						if (year_num > 1900)
						{
							found_year = 1;
							*year = strdup(tmp);
							year_pos = total_count - 5;
						}
					}
					else if (!found_season && !found_episode)
					{
						int episode_num = atoi(&tmp[2]);
						if (episode_num > 0)
							tmp[2] = '\0';
						int season_num = atoi(tmp);
						if ((season_num > 0) && (episode_num > 0))
						{
							found_episode = 1;
							found_season = 1;
							*season = strdup(tmp);
							memset(tmp, 0, 10);
							snprintf(tmp, 10, "%d", episode_num);
							*episode = strdup(tmp);
							season_pos = total_count - 5;
							episode_pos = total_count - 3;
						}
					}
				}
				else if (digit_count == 3)
				{
					if (!found_season && !found_episode)
					{
						int episode_num = atoi(&tmp[1]);
						if (episode_num > 0)
							tmp[1] = '\0';
						int season_num = atoi(tmp);
						if ((season_num > 0) && (episode_num > 0))
						{
							found_episode = 1;
							found_season = 1;
							*season = strdup(tmp);
							memset(tmp, 0, 10);
							snprintf(tmp, 10, "%d", episode_num);
							*episode = strdup(tmp);
							season_pos = total_count - 4;
							episode_pos = total_count - 3;
						}
					}
				}
				else if (digit_count == 2)
				{
					if (!found_season && (total_count > 2) && ((work[total_count - 3] == 'S') || (work[total_count - 3] == 's')))
					{
						int season_num = atoi(tmp);
						if (season_num > 0)
						{
							found_season = 1;
							season_pos = total_count - 4;
							*season = strdup(tmp);
						}
					}
					else if (found_season && !found_episode && (total_count > 2) && ((work[total_count - 3] == 'E') || (work[total_count - 3] == 'e')))
					{
						int episode_num = atoi(tmp);
						if (episode_num > 0)
						{
							found_episode = 1;
							episode_pos = total_count - 4;
							*episode = strdup(tmp);
						}
					}
				}
				else if (digit_count == 1)
				{
					if (!found_season && (total_count > 1) && ((work[total_count - 2] == 'S') || (work[total_count - 2] == 's')))
					{
						int season_num = atoi(tmp);
						if (season_num > 0)
						{
							found_season = 1;
							season_pos = total_count - 3;
							*season = strdup(tmp);
						}
					}
					else if (found_season && !found_episode && (total_count > 1) && ((work[total_count - 1] == 'E') || (work[total_count - 1] == 'e')))
					{
						int episode_num = atoi(tmp);
						if (episode_num > 0)
						{
							found_episode = 1;
							episode_pos = total_count - 3;
							*episode = strdup(tmp);
						}
					}
				}
			}
			previous = CHARACTER;
			digit_count = 0;
			memset(tmp, 0, 10);
		}

		total_count++;
	}
	/* ... if the input ends with a number ... */
	if (digit_count == 4)
	{
		if (!found_season && !found_episode)
		{
			int episode_num = atoi(&tmp[2]);
			if (episode_num > 0)
				tmp[2] = '\0';
			int season_num = atoi(tmp);
			if ((season_num > 0) && (episode_num > 0))
			{
				found_episode = 1;
				found_season = 1;
				*season = strdup(tmp);
				memset(tmp, 0, 10);
				snprintf(tmp, 10, "%d", episode_num);
				*episode = strdup(tmp);
				season_pos = total_count - 5;
				episode_pos = total_count - 3;
			}
		}
		else if (!found_year)
		{
			int year_num = atoi(tmp);
			if (year_num > 1900)
			{
				found_year = 1;
				*year = strdup(tmp);
				year_pos = total_count - 5;
			}
		}
	}
	else if (digit_count == 3)
	{
		if (!found_season && !found_episode)
		{
			int episode_num = atoi(&tmp[1]);
			if (episode_num > 0)
				tmp[1] = '\0';
			int season_num = atoi(tmp);
			if ((season_num > 0) && (episode_num > 0))
			{
				found_episode = 1;
				found_season = 1;
				*season = strdup(tmp);
				memset(tmp, 0, 10);
				snprintf(tmp, 10, "%d", episode_num);
				*episode = strdup(tmp);
				season_pos = total_count - 4;
				episode_pos = total_count - 3;
			}
		}
		else if (!found_episode) // 100+ episodes per season???
		{
			int episode_num = atoi(tmp);
			if (episode_num > 0)
			{
				found_episode = 1;
				*episode = strdup(tmp);
				episode_pos = total_count - 3;
			}
		}
	}
	else if ((digit_count > 1) && found_season && !found_episode)
	{
		int episode_num = atoi(tmp);
		if (episode_num > 0)
		{
			found_episode = 1;
			*episode = strdup(tmp);
			episode_pos = total_count - digit_count;
		}
	}

//	DPRINTF(E_DEBUG, L_GENERAL, "Found%s year [%lu], found%s season [%lu], found%s episode [%lu]\n",
//		found_year ? "" : " no", year_pos, found_season ? "" : " no", season_pos,
//		found_episode ? "" : " no", episode_pos);

	size_t last = 0;
	if (found_season)
		last = season_pos;
	if (found_episode)
	{
		if (last == 0) last = episode_pos;
		else if (episode_pos < last) last = episode_pos;
	}
	if (found_year)
	{
		if (last == 0) last = year_pos;
		else if (year_pos < last) last = year_pos;
	}
	if (last > 0)
	{
		//DEBUG DPRINTF(E_DEBUG, L_GENERAL, "Last character in name is %lu\n", last);
		*show_name = malloc(last + 1);
		memset(*show_name, 0, last + 1);
		x_strlcpy(*show_name, input, last + 1);
	}

clean_up:
	free(work);
}

/* Code basically stolen from busybox */
int
make_dir(char * path, mode_t mode)
{
	char * s = path;
	char c;
	struct stat st;

	do {
		c = '\0';

		/* Before we do anything, skip leading /'s, so we don't bother
		 * trying to create /. */
		while (*s == '/')
			++s;

		/* Bypass leading non-'/'s and then subsequent '/'s. */
		while (*s) {
			if (*s == '/') {
				do {
					++s;
				} while (*s == '/');
				c = *s;     /* Save the current char */
				*s = '\0';     /* and replace it with nul. */
				break;
			}
			++s;
		}

		if (mkdir(path, mode) < 0) {
			/* If we failed for any other reason than the directory
			 * already exists, output a diagnostic and return -1.*/
			if ((errno != EEXIST && errno != EISDIR)
			    || (stat(path, &st) < 0 || !S_ISDIR(st.st_mode))) {
				DPRINTF(E_WARN, L_GENERAL, "make_dir: cannot create directory '%s'\n", path);
				if (c)
					*s = c;
				return -1;
			}
		}
	        if (!c)
			return 0;

		/* Remove any inserted nul from the path. */
		*s = c;

	} while (1);
}

/* Simple, efficient hash function from Daniel J. Bernstein */
unsigned int
DJBHash(uint8_t *data, int len)
{
	unsigned int hash = 5381;
	unsigned int i = 0;

	for(i = 0; i < len; data++, i++)
	{
		hash = ((hash << 5) + hash) + (*data);
	}

	return hash;
}

const char *
mime_to_ext(const char * mime)
{
	switch( *mime )
	{
		/* Audio extensions */
		case 'a':
			if( strcmp(mime+6, "mpeg") == 0 )
				return "mp3";
			else if( strcmp(mime+6, "mp4") == 0 )
				return "m4a";
			else if( strcmp(mime+6, "x-ms-wma") == 0 )
				return "wma";
			else if( strcmp(mime+6, "x-flac") == 0 )
				return "flac";
			else if( strcmp(mime+6, "flac") == 0 )
				return "flac";
			else if( strcmp(mime+6, "x-wav") == 0 )
				return "wav";
			else if( strncmp(mime+6, "L16", 3) == 0 )
				return "pcm";
			else if( strcmp(mime+6, "3gpp") == 0 )
				return "3gp";
			else if( strcmp(mime, "application/ogg") == 0 )
				return "ogg";
			break;
		case 'v':
			if( strcmp(mime+6, "avi") == 0 )
				return "avi";
			else if( strcmp(mime+6, "divx") == 0 )
				return "avi";
			else if( strcmp(mime+6, "x-msvideo") == 0 )
				return "avi";
			else if( strcmp(mime+6, "mpeg") == 0 )
				return "mpg";
			else if( strcmp(mime+6, "mp4") == 0 )
				return "mp4";
			else if( strcmp(mime+6, "x-ms-wmv") == 0 )
				return "wmv";
			else if( strcmp(mime+6, "x-matroska") == 0 )
				return "mkv";
			else if( strcmp(mime+6, "x-mkv") == 0 )
				return "mkv";
			else if( strcmp(mime+6, "x-flv") == 0 )
				return "flv";
			else if( strcmp(mime+6, "vnd.dlna.mpeg-tts") == 0 )
				return "mpg";
			else if( strcmp(mime+6, "quicktime") == 0 )
				return "mov";
			else if( strcmp(mime+6, "3gpp") == 0 )
				return "3gp";
			else if( strncmp(mime+6, "x-tivo-mpeg", 11) == 0 )
				return "TiVo";
			break;
		case 'i':
			if( strcmp(mime+6, "jpeg") == 0 )
				return "jpg";
			else if( strcmp(mime+6, "png") == 0 )
				return "png";
			break;
		default:
			break;
	}
	return "dat";
}

int
is_video(const char * file)
{
	return (ends_with(file, ".mpg") || ends_with(file, ".mpeg")  ||
		ends_with(file, ".avi") || ends_with(file, ".divx")  ||
		ends_with(file, ".asf") || ends_with(file, ".wmv")   ||
		ends_with(file, ".mp4") || ends_with(file, ".m4v")   ||
		ends_with(file, ".mts") || ends_with(file, ".m2ts")  ||
		ends_with(file, ".m2t") || ends_with(file, ".mkv")   ||
		ends_with(file, ".vob") || ends_with(file, ".ts")    ||
		ends_with(file, ".flv") || ends_with(file, ".xvid")  ||
#ifdef TIVO_SUPPORT
		ends_with(file, ".TiVo") ||
#endif
		ends_with(file, ".mov") || ends_with(file, ".3gp"));
}

int
is_audio(const char * file)
{
	return (ends_with(file, ".mp3") || ends_with(file, ".flac") ||
		ends_with(file, ".wma") || ends_with(file, ".asf")  ||
		ends_with(file, ".fla") || ends_with(file, ".flc")  ||
		ends_with(file, ".m4a") || ends_with(file, ".aac")  ||
		ends_with(file, ".mp4") || ends_with(file, ".m4p")  ||
		ends_with(file, ".wav") || ends_with(file, ".ogg")  ||
		ends_with(file, ".pcm") || ends_with(file, ".3gp"));
}

int
is_image(const char * file)
{
	return (ends_with(file, ".jpg") || ends_with(file, ".jpeg"));
}

int
is_playlist(const char * file)
{
	return (ends_with(file, ".m3u") || ends_with(file, ".pls"));
}

int
is_caption(const char * file)
{
	return (ends_with(file, ".srt") || ends_with(file, ".smi"));
}

int
is_album_art(const char * name)
{
	struct album_art_name_s * album_art_name;

	/* Check if this file name matches one of the default album art names */
	for( album_art_name = album_art_names; album_art_name; album_art_name = album_art_name->next )
	{
		if( album_art_name->wildcard )
		{
			if( strncmp(album_art_name->name, name, strlen(album_art_name->name)) == 0 )
				break;
		}
		else
		{
			if( strcmp(album_art_name->name, name) == 0 )
				break;
		}
	}

	return (album_art_name ? 1 : 0);
}

int
resolve_unknown_type(const char * path, media_types dir_type)
{
	struct stat entry;
	unsigned char type = TYPE_UNKNOWN;
	char str_buf[PATH_MAX];
	ssize_t len;

	if( lstat(path, &entry) == 0 )
	{
		if( S_ISLNK(entry.st_mode) )
		{
			if( (len = readlink(path, str_buf, PATH_MAX-1)) > 0 )
			{
				str_buf[len] = '\0';
				//DEBUG DPRINTF(E_DEBUG, L_GENERAL, "Checking for recursive symbolic link: %s (%s)\n", path, str_buf);
				if( strncmp(path, str_buf, strlen(str_buf)) == 0 )
				{
					DPRINTF(E_DEBUG, L_GENERAL, "Ignoring recursive symbolic link: %s (%s)\n", path, str_buf);
					return type;
				}
			}
			stat(path, &entry);
		}

		if( S_ISDIR(entry.st_mode) )
		{
			type = TYPE_DIR;
		}
		else if( S_ISREG(entry.st_mode) )
		{
			switch( dir_type )
			{
				case ALL_MEDIA:
					if( is_image(path) ||
					    is_audio(path) ||
					    is_video(path) ||
					    is_playlist(path) )
						type = TYPE_FILE;
					break;
				case TYPE_AUDIO:
					if( is_audio(path) ||
					    is_playlist(path) )
						type = TYPE_FILE;
					break;
				case TYPE_VIDEO:
					if( is_video(path) )
						type = TYPE_FILE;
					break;
				case TYPE_IMAGES:
					if( is_image(path) )
						type = TYPE_FILE;
					break;
				default:
					break;
			}
		}
	}
	return type;
}

