//
// Created by ttodorov on 2/22/17.
//
#include "config.h"

#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <jansson.h>
#include <curl/curl.h>
#include "upnpglobalvars.h"
#include "log.h"
#include "metadata.h"
#include "metadata_ext.h"
#include "utils.h"
#include "sql.h"
#include "image_utils.h"
#include "albumart.h"

#define URL_SMALL_BUFFER_SZ        	16384   // 16 KB
#define URL_MEDIUM_BUFFER_SZ		65536   // 65 KB
#define URL_BIG_BUFFER_SZ		262144  // 256 KB
#define URL_HUGE_BUFFER_SZ              524288  // 512 KB

#define URL_MOVIE_DB_SEARCH_MOVIES	"https://api.themoviedb.org/3/search/movie?api_key=%s&query=%s"
#define URL_MOVIE_DB_DETAIL_MOVIES	"https://api.themoviedb.org/3/movie/%ld?api_key=%s"
#define URL_MOVIE_DB_CREDIT_MOVIES	"https://api.themoviedb.org/3/movie/%ld/credits?api_key=%s"
#define URL_MOVIE_DB_IMGART_MOVIES	"%s%s%s?api_key=%s"
#define URL_MOVIE_DB_SEARCH_TVSHOW	"https://api.themoviedb.org/3/search/tv?api_key=%s&query=%s"
#define URL_MOVIE_DB_QUERY_WITH_YEAR    "%s&year=%s"
#define URL_MOVIE_DB_CONFIGURATION	"https://api.themoviedb.org/3/configuration?api_key=%s"

struct http_response
{
	char *data;
	int pos;
	int max_size;
};

static bool curl_is_initialized = false;

void
init_ext_meta()
{
	curl_version_info_data *curl_ver;

	if (!GETFLAG(EXT_META_MASK))
		return;

        if (curl_is_initialized)
        {
                DPRINTF(E_WARN, L_HTTP, "External metadata facility already initialized.\n");
                return;
        }

	if (strlen(the_moviedb_api_key) == 0)
	{
		DPRINTF(E_ERROR, L_HTTP, "API key for accessing external metadata is not configured.\n");
		return;
	}

	DPRINTF(E_INFO, L_HTTP, "Initializing external metadata facility...\n");

	curl_ver = curl_version_info(CURLVERSION_NOW);
	DPRINTF(E_DEBUG, L_HTTP, "Using libCURL version %s.\n", curl_ver->version);
	curl_global_init(CURL_GLOBAL_ALL);
	curl_is_initialized = true;
}

static size_t
ext_meta_response(void *ptr, size_t size, size_t nmemb, void *stream)
{
	struct http_response *result = (struct http_response *)stream;

	if(result->pos + size * nmemb >= result->max_size - 1)
	{
		DPRINTF(E_ERROR, L_HTTP, "External metadata response buffer is too small (required %ld)\n", result->pos + size * nmemb);
		return 0;
	}

	memcpy(result->data + result->pos, ptr, size * nmemb);
	result->pos += size * nmemb;

	return size * nmemb;
}

static int64_t
movie_db_search_movies(CURL *conn, const char *search_crit, const char *year, metadata_t *meta, uint32_t *metaflags)
{
	CURLcode fetch_status;
	long http_resp_code;
	char *http_data = NULL;
	char *tmp_str = NULL;
	char query[MAXPATHLEN];
	int64_t ret = 0;
	json_t *js_root = NULL;
	json_error_t js_err;

	memset(query, 0, sizeof(query));

	http_data = malloc(URL_SMALL_BUFFER_SZ);
	if (!http_data)
		return -1;
	struct http_response write_response = {
		.data = http_data,
		.pos = 0,
		.max_size = URL_SMALL_BUFFER_SZ
	};
	curl_easy_setopt(conn, CURLOPT_WRITEDATA, &write_response);

	/* compile query */
	tmp_str = curl_easy_escape(conn, search_crit, 0);
	snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_MOVIES, the_moviedb_api_key, tmp_str);
	curl_free(tmp_str);
	if (year)
	{
		tmp_str = strdup(query);
		snprintf(query, MAXPATHLEN, URL_MOVIE_DB_QUERY_WITH_YEAR, tmp_str, year);
		free(tmp_str);
	}
	DPRINTF(E_DEBUG, L_HTTP, "External metadata search URL: %s\n", query);
	curl_easy_setopt(conn, CURLOPT_URL, query);

	fetch_status = curl_easy_perform(conn);
	if (fetch_status != CURLE_OK)
	{
		DPRINTF(E_ERROR, L_HTTP, "External metadata search error: %s\n", curl_easy_strerror(fetch_status));
		ret = -2;
		goto movie_search_error;
	}
	curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_resp_code);
	if (http_resp_code != 200)
	{
		DPRINTF(E_WARN, L_HTTP, "External metadata search response: %ld\n", http_resp_code);
	}
	http_data[write_response.pos] = '\0';
	DPRINTF(E_DEBUG, L_METADATA, "External metadata search response: %s\n", http_data);

	js_root = json_loads(http_data, 0, &js_err);
	if (!js_root)
	{
		DPRINTF(E_ERROR, L_METADATA, "JSON conversion failed: [%d] %s\n", js_err.line, js_err.text);
		ret = -3;
		goto movie_search_error;
	}
	if (!json_is_object(js_root))
	{
		ret = -3;
		goto movie_search_error;
	}
	json_t *object = json_object_get(js_root, "total_results");
	if (!json_is_integer(object))
	{
		ret = -3;
		goto movie_search_error;
	}
	json_int_t db_total_results = json_integer_value(object);
	if (db_total_results > 0)
	{
		object = json_object_get(js_root, "results");
		if (!json_is_array(object))
		{
			ret = -3;
			goto movie_search_cleanup;
		}
		json_t *record = json_array_get(object, 0);
		object = json_object_get(record, "title");
		if (json_is_string(object))
		{
			meta->title = strdup(json_string_value(object));
			*metaflags |= FLAG_TITLE;
		}
		object = json_object_get(record, "release_date");
		if (json_is_string(object))
		{
			meta->date = strdup(json_string_value(object));
			*metaflags |= FLAG_DATE;
		}
		object = json_object_get(record, "overview");
		if (json_is_string(object))
		{
			meta->description = strdup(json_string_value(object));
			*metaflags |= FLAG_DESCRIPTION;
		}
		object = json_object_get(record, "id");
		if (!json_is_integer(object))
		{
			ret = -3;
			goto movie_search_error;
		}
		ret = json_integer_value(object);
	}
	goto movie_search_cleanup;

movie_search_error:
	DPRINTF(E_ERROR, L_METADATA, "Failed to parse external metadata search response.\n");

movie_search_cleanup:
	if (http_data)
		free(http_data);
	if (js_root)
		json_decref(js_root);
	return ret;
}

static int64_t
movie_db_get_movie_details(CURL *conn, int64_t movie_id, metadata_t *meta, uint32_t *metaflags, char **artwork_path)
{
	CURLcode fetch_status;
	long http_resp_code;
	char *http_data = NULL;
	char query[MAXPATHLEN];
	int64_t ret = 0;
	json_t *js_root = NULL;
	json_error_t js_err;
	char helper[64];

	memset(query, 0, sizeof(query));
	memset(helper, 0, sizeof(helper));

	http_data = malloc(URL_SMALL_BUFFER_SZ);
	if (!http_data)
		return -1;
	struct http_response write_response = {
		.data = http_data,
		.pos = 0,
		.max_size = URL_SMALL_BUFFER_SZ
	};
	curl_easy_setopt(conn, CURLOPT_WRITEDATA, &write_response);

	/* compile query */
	snprintf(query, MAXPATHLEN, URL_MOVIE_DB_DETAIL_MOVIES, movie_id, the_moviedb_api_key);
	DPRINTF(E_DEBUG, L_HTTP, "External metadata detail URL: %s\n", query);
	curl_easy_setopt(conn, CURLOPT_URL, query);

	fetch_status = curl_easy_perform(conn);
	if (fetch_status != CURLE_OK)
	{
		DPRINTF(E_ERROR, L_HTTP, "External metadata detail error: %s\n", curl_easy_strerror(fetch_status));
		ret = -2;
		goto movie_detail_error;
	}
	curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_resp_code);
	if (http_resp_code != 200)
	{
		DPRINTF(E_WARN, L_HTTP, "External metadata details response: %ld\n", http_resp_code);
	}
	http_data[write_response.pos] = '\0';
	DPRINTF(E_DEBUG, L_METADATA, "External metadata details response: %s\n", http_data);

	js_root = json_loads(http_data, 0, &js_err);
	if (!js_root)
	{
		DPRINTF(E_ERROR, L_METADATA, "JSON conversion failed: [%d] %s\n", js_err.line, js_err.text);
		ret = -3;
		goto movie_detail_error;
	}
	if (!json_is_object(js_root))
	{
		ret = -3;
		goto movie_detail_error;
	}
	json_t *object = json_object_get(js_root, "tagline");
	if (json_is_string(object))
	{
		meta->comment = strdup(json_string_value(object));
		*metaflags |= FLAG_COMMENT;
	}
	object = json_object_get(js_root, "vote_average");
	if (json_is_real(object))
	{
		snprintf(helper, 64, "%f/10", json_real_value(object));
		meta->rating = strdup(helper);
		*metaflags |= FLAG_RATING;
	}
	object = json_object_get(js_root, "genres");
	if (json_is_array(object))
	{
		memset(helper, 0, sizeof(helper));
		int genres_count = json_array_size(object);
		for (int gpos = 0; gpos < genres_count; gpos++)
		{
			json_t *record = json_array_get(object, gpos);
			json_t *genre_id = json_object_get(record, "id");
			json_t *genre_name = json_object_get(record, "name");
			if (json_is_integer(genre_id) && json_is_string(genre_name))
			{
				x_strlcat(helper, json_string_value(genre_name), sizeof(helper));
				if (gpos < genres_count - 1)
					x_strlcat(helper, "|", sizeof(helper));
			}
		}
		if (strlen(helper) > 0)
		{
			meta->genre = strdup(helper);
			*metaflags |= FLAG_GENRE;
		}
	}
	object = json_object_get(js_root, "poster_path");
	if (json_is_string(object))
	{
		*artwork_path = strdup(json_string_value(object));
	}
	if (!(*metaflags & FLAG_DATE))
	{
		object = json_object_get(js_root, "release_date");
		if (json_is_string(object))
		{
			meta->date = strdup(json_string_value(object));
			*metaflags |= FLAG_DATE;
		}
	}
	goto movie_detail_cleanup;

movie_detail_error:
	DPRINTF(E_ERROR, L_METADATA, "Failed to parse external metadata response.\n");

movie_detail_cleanup:
	if (http_data) free(http_data);
	if (js_root) json_decref(js_root);
	return ret;
}

static int64_t
movie_db_get_movie_credits(CURL *conn, int64_t movie_id, metadata_t *meta, uint32_t *metaflags)
{
	CURLcode fetch_status;
	long http_resp_code;
	char *http_data = NULL;
	char query[MAXPATHLEN];
	int64_t ret = 0;
	json_t *js_root = NULL;
	json_error_t js_err;
	char helper[64];

	memset(query, 0, sizeof(query));
	memset(helper, 0, sizeof(helper));

	http_data = malloc(URL_MEDIUM_BUFFER_SZ);
	if (!http_data)
		return -1;
	struct http_response write_response = {
		.data = http_data,
		.pos = 0,
		.max_size = URL_MEDIUM_BUFFER_SZ
	};
	curl_easy_setopt(conn, CURLOPT_WRITEDATA, &write_response);

	/* compile query */
	snprintf(query, MAXPATHLEN, URL_MOVIE_DB_CREDIT_MOVIES, movie_id, the_moviedb_api_key);
	DPRINTF(E_DEBUG, L_HTTP, "External metadata credits URL: %s\n", query);
	curl_easy_setopt(conn, CURLOPT_URL, query);

	fetch_status = curl_easy_perform(conn);
	if (fetch_status != CURLE_OK)
	{
		DPRINTF(E_ERROR, L_HTTP, "External metadata credits error: %s\n", curl_easy_strerror(fetch_status));
		ret = -2;
		goto movie_credits_error;
	}
	curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_resp_code);
	if (http_resp_code != 200)
	{
		DPRINTF(E_WARN, L_HTTP, "External metadata credits response: %ld\n", http_resp_code);
	}
	http_data[write_response.pos] = '\0';
	DPRINTF(E_DEBUG, L_METADATA, "External metadata credits response: %s\n", http_data);

	js_root = json_loads(http_data, 0, &js_err);
	if (!js_root)
	{
		DPRINTF(E_ERROR, L_METADATA, "JSON conversion failed: [%d] %s\n", js_err.line, js_err.text);
		ret = -3;
		goto movie_credits_error;
	}
	if (!json_is_object(js_root))
	{
		ret = -3;
		goto movie_credits_error;
	}

	json_t *cast = json_object_get(js_root, "cast");
	if (json_is_array(cast))
	{
		char movie_cast[1024] = {0};
		int cast_total = json_array_size(cast);
		for (int current = 0; current < cast_total; current++)
		{
			json_t *actor = json_array_get(cast, current);
			json_t *actor_name = json_object_get(actor, "name");
			if (json_is_string(actor_name))
			{
				x_strlcat(movie_cast, json_string_value(actor_name), 1024);
				x_strlcat(movie_cast, ",", 1024);
			}
		}
		movie_cast[1023] = '\0';
		/* do not end with a comma */
		size_t len = strlen(movie_cast);
		if (len > 0)
		{
			if (movie_cast[len-1] == ',')
				movie_cast[len-1] = '\0';
			meta->artist = strdup(movie_cast);
			*metaflags |= FLAG_ARTIST;
		}
	}
	json_t *crew = json_object_get(js_root, "crew");
	if (json_is_array(crew))
	{
		char movie_producers[255] = {0};
		char movie_directors[255] = {0};
		char movie_writers[255] = {0};
		int crew_total = json_array_size(crew);
		for (int next = 0; next < crew_total; next++)
		{
			json_t *contrib = json_array_get(crew, next);
			json_t *contrib_name = json_object_get(contrib, "name");
			json_t *job = json_object_get(contrib, "department");
			if (json_is_string(contrib_name) && json_is_string(job))
			{
				if (strcasestr(json_string_value(job), "Directing"))
				{
					x_strlcat(movie_directors, json_string_value(contrib_name), 255);
					x_strlcat(movie_directors, ",", 255);
				}
				else if (strcasestr(json_string_value(job), "Writing"))
				{
					x_strlcat(movie_writers, json_string_value(contrib_name), 255);
					x_strlcat(movie_writers, ",", 255);
				}
				else if (strcasestr(json_string_value(job), "Production"))
				{
					x_strlcat(movie_producers, json_string_value(contrib_name), 255);
					x_strlcat(movie_producers, ",", 255);
				}
			}
		}
		movie_producers[254] = '\0';
		movie_directors[254] = '\0';
		movie_writers[254] = '\0';
		/* do not end with a comma */
		size_t len = strlen(movie_producers);
		if (len > 0)
		{
			if (movie_producers[len-1] == ',')
				movie_producers[len-1] = '\0';
			meta->publisher = strdup(movie_producers);
			*metaflags |= FLAG_PUBLISHER;
		}
		len = strlen(movie_directors);
		if (len > 0)
		{
			if (movie_directors[len-1] == ',')
				movie_directors[len-1] = '\0';
			meta->creator = strdup(movie_directors);
			*metaflags |= FLAG_CREATOR;
		}
		len = strlen(movie_writers);
		if (len > 0)
		{
			if (movie_writers[len-1] == ',')
				movie_writers[len-1] = '\0';
			meta->author = strdup(movie_writers);
			*metaflags |= FLAG_AUTHOR;
		}
	}

	goto movie_credits_cleanup;

movie_credits_error:
	DPRINTF(E_ERROR, L_METADATA, "Failed to parse external metadata response.\n");

movie_credits_cleanup:
	if (http_data) free(http_data);
	if (js_root) json_decref(js_root);
	return ret;
}

static int64_t
movie_db_get_configuration(CURL *conn, char **base_url, char **img_sz)
{
	CURLcode fetch_status;
	long http_resp_code;
	char *http_data = NULL;
	char query[MAXPATHLEN];
	int64_t ret = 0;
	json_t *js_root = NULL;
	json_error_t js_err;
	char helper[64];

	memset(query, 0, sizeof(query));
	memset(helper, 0, sizeof(helper));

	http_data = malloc(URL_SMALL_BUFFER_SZ);
	if (!http_data)
		return -1;
	struct http_response write_response = {
		.data = http_data,
		.pos = 0,
		.max_size = URL_SMALL_BUFFER_SZ
	};
	curl_easy_setopt(conn, CURLOPT_WRITEDATA, &write_response);

	/* compile query */
	snprintf(query, MAXPATHLEN, URL_MOVIE_DB_CONFIGURATION, the_moviedb_api_key);
	DPRINTF(E_DEBUG, L_HTTP, "External metadata configuration URL: %s\n", query);
	curl_easy_setopt(conn, CURLOPT_URL, query);

	fetch_status = curl_easy_perform(conn);
	if (fetch_status != CURLE_OK)
	{
		DPRINTF(E_ERROR, L_HTTP, "External metadata configuration error: %s\n", curl_easy_strerror(fetch_status));
		ret = -2;
		goto movie_configuration_error;
	}
	curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_resp_code);
	if (http_resp_code != 200)
	{
		DPRINTF(E_WARN, L_HTTP, "External metadata configuration response: %ld\n", http_resp_code);
	}
	http_data[write_response.pos] = '\0';
	DPRINTF(E_DEBUG, L_METADATA, "External metadata configuration response: %s\n", http_data);

	js_root = json_loads(http_data, 0, &js_err);
	if (!js_root)
	{
		DPRINTF(E_ERROR, L_METADATA, "JSON conversion failed: [%d] %s\n", js_err.line, js_err.text);
		ret = -3;
		goto movie_configuration_error;
	}
	if (!json_is_object(js_root))
	{
		ret = -3;
		goto movie_configuration_error;
	}
	json_t *images = json_object_get(js_root, "images");
	if (!json_is_object(images))
	{
		ret = -3;
		goto movie_configuration_error;
	}
	json_t *object = json_object_get(images, "secure_base_url");
	if (!json_is_string(object))
	{
		ret = -3;
		goto movie_configuration_error;
	}
	*base_url = strdup(json_string_value(object));
	object = json_object_get(images, "poster_sizes");
	if (!json_is_array(object))
	{
		object = json_object_get(images, "backdrop_sizes");
		if (!json_is_array(object))
		{
			ret = -3;
			goto movie_configuration_error;
		}
	}
	int img_sizes_count = json_array_size(object);
	if (img_sizes_count <= 0)
	{
		ret = -3;
		goto movie_configuration_error;
	}
	int last_acceptable = 0;
	const char *last_selection = NULL;
	for (int current = 0; current < img_sizes_count; current++)
	{
		json_t *item = json_array_get(object, current);
		if (json_is_string(item))
		{
			const char *test = json_string_value(item);
			if (external_meta_img_sz > 0)
			{
				while (*test != '\0' && isalpha(*test))
					test++;
				if (*test != '\0')
				{
					int num = strtol(test, NULL, 10);
					if (num <= external_meta_img_sz && num > last_acceptable)
					{
						last_acceptable = num;
						last_selection = json_string_value(item);
					}
					else
						break;
				}
				else
				{
					/* we obviously reached a value not convertible to a number
					 * before the last acceptable value is past the configured
					 * size, so we should use this (a.k.a 'original' or some
					 * such size)
					 */
					last_selection = json_string_value(item);
					break;
				}
			}
			else
			{
				/* configuration value is not set */
				last_selection = test;
				break;
			}
		}
	}
	if (last_selection)
		*img_sz = strdup(last_selection);
	else
		ret = -4;

	goto movie_configuration_cleanup;

movie_configuration_error:
	DPRINTF(E_ERROR, L_METADATA, "Failed to parse external metadata response.\n");

movie_configuration_cleanup:
	if (http_data) free(http_data);
	if (js_root) json_decref(js_root);
	return ret;
}

static int64_t
movie_db_get_imageart(CURL *conn, const char *base_url, const char *img_size_sel, const char *img_path, uint8_t **img, int *img_sz)
{
	CURLcode fetch_status;
	long http_resp_code;
	char *http_data = NULL;
	char query[MAXPATHLEN];
	int64_t ret = 0;

	memset(query, 0, sizeof(query));

	http_data = malloc(URL_HUGE_BUFFER_SZ);
	if (!http_data)
		return -1;
	struct http_response write_response = {
		.data = http_data,
		.pos = 0,
		.max_size = URL_HUGE_BUFFER_SZ
	};
	curl_easy_setopt(conn, CURLOPT_WRITEDATA, &write_response);

	/* compile query */
	snprintf(query, MAXPATHLEN, URL_MOVIE_DB_IMGART_MOVIES, base_url, img_size_sel, img_path, the_moviedb_api_key);
	DPRINTF(E_DEBUG, L_HTTP, "External metadata imageart URL: %s\n", query);
	curl_easy_setopt(conn, CURLOPT_URL, query);

	fetch_status = curl_easy_perform(conn);
	if (fetch_status != CURLE_OK)
	{
		DPRINTF(E_ERROR, L_HTTP, "External metadata imageart error: %s\n", curl_easy_strerror(fetch_status));
		ret = -2;
		goto movie_imgart_cleanup;
	}
	DPRINTF(E_DEBUG, L_HTTP, "Retreived %d bytes image art\n", write_response.pos);
	curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_resp_code);
	if (http_resp_code != 200)
	{
		DPRINTF(E_WARN, L_HTTP, "External metadata imageart response: %ld\n", http_resp_code);
	}
	if (write_response.pos)
	{
		*img_sz = write_response.pos;
		*img = malloc(*img_sz);
		memcpy(*img, http_data, *img_sz);
	}

movie_imgart_cleanup:
	if (http_data) free(http_data);
	return ret;
}

int64_t
search_ext_meta(uint8_t is_tv, const char *path, char *name, int64_t detailID, uint8_t **img, int *img_sz)
{
	int64_t result = 0;
	CURL *fetch = NULL;
	struct curl_slist *headers = NULL;

	struct stat file;
	char *clean_name = NULL;
	char *year = NULL;
	char *show = NULL;
	char *season = NULL;
	char *episode = NULL;

	metadata_t m_data;
	uint32_t m_flags = FLAG_MIME|FLAG_DURATION|FLAG_DLNA_PN|FLAG_DATE;
	char *video_poster = NULL;
	char *base_art_url = NULL;
	char *art_sz_str = NULL;

	memset(&m_data, '\0', sizeof(m_data));

	if ( stat(path, &file) != 0 )
		return 0;
	strip_ext(name);

	fetch = curl_easy_init();
	if (!fetch) goto curl_error;

	curl_easy_setopt(fetch, CURLOPT_HTTPGET, 1L);

	headers = curl_slist_append(headers, "User-Agent: minidlna");
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, "Accept: application/json");
	curl_easy_setopt(fetch, CURLOPT_HTTPHEADER, headers);

	curl_easy_setopt(fetch, CURLOPT_WRITEFUNCTION, ext_meta_response);

	DPRINTF(E_DEBUG, L_METADATA, "Trying to find online metadata for %s.\n", name);

	if (is_tv)
	{
		clean_name = cleanup_name(name, "._");
		split_tv_name(clean_name, &show, &year, &season, &episode);
		DPRINTF(E_DEBUG, L_METADATA, "Searching for %s - %s - %s:%s.\n", show ? show : "[no name]",
			year ? year : "[19xx-20xx]", season ? season : "[unknown season]",
			episode ? episode : "[unknown episode]");
	}
	else
	{
		clean_name = cleanup_name(name, ".-_");
		year = strip_year(clean_name);

		result = movie_db_search_movies(fetch, clean_name, year, &m_data, &m_flags);
		if (result > 0)
		{
			if (movie_db_get_movie_details(fetch, result, &m_data, &m_flags, &video_poster) == 0)
			{
				DPRINTF(E_DEBUG, L_METADATA, "Received movie poster path %s, and genre(s) %s\n",
					video_poster != NULL ? video_poster : "[none exists]", m_flags & FLAG_GENRE ? m_data.genre : "[unknown]");
			}
			if (movie_db_get_movie_credits(fetch, result, &m_data, &m_flags) == 0)
			{
				DPRINTF(E_DEBUG, L_METADATA, "Received movie credits for %s\n", m_data.title);
			}
			if (video_poster && strlen(video_poster) && movie_db_get_configuration(fetch, &base_art_url, &art_sz_str) == 0)
			{
				/* ready to download the poster image */
				movie_db_get_imageart(fetch, base_art_url, art_sz_str, video_poster, &m_data.thumb_data, &m_data.thumb_size);
			}
		}
	}

	if (result > 0)
	{
		int ret_sql;
		int64_t img_art = 0;
		if (m_data.thumb_size)
		{
//			DPRINTF(E_DEBUG, L_METADATA, "Checking if movie poster for path %s is saved...\n", path);
//			img_art = find_album_art(path, m_data.thumb_data, m_data.thumb_size);
			*img = m_data.thumb_data;
			*img_sz = m_data.thumb_size;
		}

		if (detailID)
		{
			ret_sql = sql_exec(db, "UPDATE DETAILS set TITLE = '%q', DATE = %Q, DESCRIPTION = %Q, "
						   "GENRE = %Q, COMMENT = %Q, ALBUM_ART = %lld, CREATOR = %Q, PUBLISHER = %Q, "
						   "AUTHOR = %Q, ARTIST = %Q WHERE PATH=%Q and ID=%lld;", m_data.title, m_data.date,
					   m_data.description, m_data.genre, m_data.comment, img_art, m_data.creator,
					   m_data.publisher, m_data.author, m_data.artist, path, detailID);
		}
		else
		{
			ret_sql = sql_exec(db, "INSERT into DETAILS"
						   " (PATH, SIZE, TIMESTAMP, DATE, TITLE, DESCRIPTION, MIME, COMMENT, GENRE, ALBUM_ART,"
						   " CREATOR, PUBLISHER, AUTHOR, ARTIST) "
						   "VALUES (%Q, %lld, %lld, %Q, '%q', %Q, '%q', %Q, %Q, %lld, %Q, %Q, %Q, %Q);",
					   path, (long long)file.st_size, (long long)file.st_mtime,
					   m_data.date, m_data.title, m_data.description, m_data.mime, m_data.comment,
					   m_data.genre, img_art, m_data.creator, m_data.publisher, m_data.author, m_data.artist);
		}
		if( ret_sql != SQLITE_OK )
		{
			DPRINTF(E_ERROR, L_METADATA, "Error inserting/updating details for '%s'!\n", path);
		}
		else
		{
			detailID = sqlite3_last_insert_rowid(db);
		}
	}

	result = detailID;

	goto cleanup;

curl_error:
	DPRINTF(E_ERROR, L_METADATA, "Failed to process online metadata for %s.\n", clean_name);

cleanup:
	if (headers) curl_slist_free_all(headers);
	if (fetch) curl_easy_cleanup(fetch);
	if (year) free(year);
	if (clean_name) free(clean_name);
	if (show) free(show);
	if (season) free(season);
	if (episode) free(episode);
	if (base_art_url) free(base_art_url);
	if (art_sz_str) free(art_sz_str);
	free_metadata(&m_data, m_flags);

	return result;
}
