//
// Created by ttodorov on 2/22/17.
//
#include "config.h"

#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <jansson.h>
#include <curl/curl.h>
#include "upnpglobalvars.h"
#include "log.h"
#include "metadata.h"
#include "metadata_ext.h"
#include "utils.h"
#include "sql.h"

typedef long long i64;

#define URL_SMALL_BUFFER_SZ        	16384   // 16 KB
#define URL_MEDIUM_BUFFER_SZ		65536   // 65 KB
#define URL_BIG_BUFFER_SZ		524288  // 512 KB
#define URL_HUGE_BUFFER_SZ              1048576 // 1 MB

#define URL_MOVIE_DB_SEARCH_MOVIES	"https://api.themoviedb.org/3/search/movie?api_key=%s&query=%s"
#define URL_MOVIE_DB_SEARCH_MOVIES_YEAR "%s&year=%d"
#define URL_MOVIE_DB_DETAIL_MOVIES	"https://api.themoviedb.org/3/movie/%ld?api_key=%s"
#define URL_MOVIE_DB_CREDIT_MOVIES	"https://api.themoviedb.org/3/movie/%ld/credits?api_key=%s"
#define URL_MOVIE_DB_IMGART_MOVIES	"%s%s%s?api_key=%s"
#define URL_MOVIE_DB_SEARCH_TVSHOW	"https://api.themoviedb.org/3/search/tv?api_key=%s&query=%s"
#define URL_MOVIE_DB_SEARCH_TVSHOW_YEAR "%s&first_air_date_year=%d"
#define URL_MOVIE_DB_SEARCH_NEXT_PAGE	"%s&page=%lld"
#define URL_MOVIE_DB_DETAIL_TVSHOW	"https://api.themoviedb.org/3/tv/%ld?api_key=%s"
#define URL_MOVIE_DB_DETAIL_TVEPISODE	"https://api.themoviedb.org/3/tv/%ld/season/%d/episode/%d?api_key=%s"
#define URL_MOVIE_DB_CREDIT_TVEPISODE	"https://api.themoviedb.org/3/tv/%ld/season/%d/episode/%d/credits?api_key=%s"
#define URL_MOVIE_DB_CONFIGURATION	"https://api.themoviedb.org/3/configuration?api_key=%s"

struct http_response
{
	char *data;
	size_t pos;
	size_t max_size;
};

static bool curl_is_initialized = false;

static void
parse_and_wait_until(struct curl_slist *headers)
{
	struct timespec now;
	time_t retry = 0;

	DPRINTF(E_WARN, L_HTTP, "Reached the rate limit for the external metadata server, will retry in a moment...");
	struct curl_slist *s;
	for (s = headers; s != NULL; s = s->next)
	{
		char *match = strstr(s->data, "X-RateLimit-Reset: ");
		if (match != NULL)
		{
			match += strlen("X-RateLimit-Reset: ");
			retry = strtol(match, NULL, 10);
			break;
		}
	}

	if (retry > 0)
	{
		clock_gettime(CLOCK_REALTIME, &now);
		if (retry > now.tv_sec)
			sleep(retry - now.tv_sec);
	}
}

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
process_http_meta_response(void *ptr, size_t size, size_t nmemb, void *stream)
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

static size_t
process_http_response_headers(void *ptr, size_t size, size_t nmemb, void *stream)
{
	size_t header_len = size * nmemb;
	char *hdr = malloc(header_len + 1);
	memset(hdr, 0, header_len + 1);
	memcpy(hdr, ptr, header_len);
	stream = curl_slist_append((struct curl_slist*) stream, hdr);
	free(hdr);
	return header_len;
}

static i64
movie_db_search_movies(CURL *conn, const char *search_crit, int year, metadata_t *meta, uint32_t *metaflags)
{
	CURLcode fetch_status;
	long http_resp_code;
	char *http_data = NULL;
	struct curl_slist *resp_hdrs = NULL;
	char *tmp_str = NULL;
	char query[MAXPATHLEN];
	i64 ret = 0;
	json_t *js_root = NULL;
	json_error_t js_err;

	memset(query, 0, sizeof(query));

	http_data = malloc(URL_SMALL_BUFFER_SZ);
	if (!http_data)
		return 0;
	struct http_response write_response = {
		.data = http_data,
		.pos = 0,
		.max_size = URL_SMALL_BUFFER_SZ
	};
	curl_easy_setopt(conn, CURLOPT_WRITEDATA, &write_response);
	curl_easy_setopt(conn, CURLOPT_HEADERDATA, resp_hdrs);

	/* compile query */
	tmp_str = curl_easy_escape(conn, search_crit, 0);
	snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_MOVIES, the_moviedb_api_key, tmp_str);
	curl_free(tmp_str);
	if (year)
	{
		tmp_str = strdup(query);
		snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_MOVIES_YEAR, tmp_str, year);
		free(tmp_str);
	}
	DPRINTF(E_DEBUG, L_HTTP, "External metadata search URL: %s\n", query);
	curl_easy_setopt(conn, CURLOPT_URL, query);

http_movie_search_request:
	resp_hdrs = curl_slist_append(resp_hdrs, ""); // initialize
	fetch_status = curl_easy_perform(conn);
	if (fetch_status != CURLE_OK)
	{
		DPRINTF(E_ERROR, L_HTTP, "External metadata search error: %s\n", curl_easy_strerror(fetch_status));
		goto movie_search_error;
	}
	curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_resp_code);
	if (http_resp_code != 200)
	{
		DPRINTF(E_WARN, L_HTTP, "External metadata search response code: %ld\n", http_resp_code);
		if (http_resp_code == 429)
		{
			parse_and_wait_until(resp_hdrs);
			curl_slist_free_all(resp_hdrs);
			memset(http_data, 0, write_response.pos);
			write_response.pos = 0;
			goto http_movie_search_request;
		}
	}
	http_data[write_response.pos] = '\0';
	DPRINTF(E_DEBUG, L_METADATA, "External metadata search response: %s\n", http_data);

	js_root = json_loads(http_data, 0, &js_err);
	if (!js_root)
	{
		DPRINTF(E_ERROR, L_METADATA, "JSON conversion failed: [%d] %s\n", js_err.line, js_err.text);
		goto movie_search_error;
	}
	if (!json_is_object(js_root))
	{
		goto movie_search_error;
	}
	json_t *object = json_object_get(js_root, "total_results");
	if (!object || !json_is_integer(object))
	{
		goto movie_search_error;
	}
	json_int_t db_total_results = json_integer_value(object);
	if (db_total_results > 0)
	{
		object = json_object_get(js_root, "results");
		if (!object || !json_is_array(object) || !json_array_size(object))
		{
			goto movie_search_cleanup;
		}
		json_t *record = json_array_get(object, 0);
		object = json_object_get(record, "title");
		if (object && json_is_string(object) && json_string_length(object))
		{
			meta->title = strdup(json_string_value(object));
			*metaflags |= FLAG_TITLE;
		}
		object = json_object_get(record, "release_date");
		if (object && json_is_string(object) && json_string_length(object))
		{
			meta->date = strdup(json_string_value(object));
			*metaflags |= FLAG_DATE;
		}
		object = json_object_get(record, "overview");
		if (object && json_is_string(object) && json_string_length(object))
		{
			meta->description = strdup(json_string_value(object));
			*metaflags |= FLAG_DESCRIPTION;
		}
		object = json_object_get(record, "id");
		if (!object || !json_is_integer(object))
		{
			goto movie_search_error;
		}
		ret = json_integer_value(object);
	}
	goto movie_search_cleanup;

movie_search_error:
	DPRINTF(E_ERROR, L_METADATA, "Failed to parse external metadata search response.\n");

movie_search_cleanup:
	if (http_data) free(http_data);
	if (resp_hdrs) curl_slist_free_all(resp_hdrs);
	if (js_root) json_decref(js_root);
	return ret;
}

static int64_t
movie_db_get_movie_details(CURL *conn, int64_t movie_id, metadata_t *meta, uint32_t *metaflags, struct album_art_name_s **artwork)
{
	CURLcode fetch_status;
	long http_resp_code;
	char *http_data = NULL;
	struct curl_slist *resp_hdrs = NULL;
	char query[MAXPATHLEN];
	int64_t ret = 0;
	json_t *js_root = NULL;
	json_error_t js_err;
	char helper[64];

	memset(query, 0, sizeof(query));
	memset(helper, 0, sizeof(helper));

	http_data = malloc(URL_SMALL_BUFFER_SZ);
	if (!http_data)
		return 0;
	struct http_response write_response = {
		.data = http_data,
		.pos = 0,
		.max_size = URL_SMALL_BUFFER_SZ
	};
	curl_easy_setopt(conn, CURLOPT_WRITEDATA, &write_response);
	curl_easy_setopt(conn, CURLOPT_HEADERDATA, resp_hdrs);

	/* compile query */
	snprintf(query, MAXPATHLEN, URL_MOVIE_DB_DETAIL_MOVIES, movie_id, the_moviedb_api_key);
	DPRINTF(E_DEBUG, L_HTTP, "External metadata detail URL: %s\n", query);
	curl_easy_setopt(conn, CURLOPT_URL, query);

http_movie_details_request:
	resp_hdrs = curl_slist_append(resp_hdrs, ""); // initialize
	fetch_status = curl_easy_perform(conn);
	if (fetch_status != CURLE_OK)
	{
		DPRINTF(E_ERROR, L_HTTP, "External metadata detail error: %s\n", curl_easy_strerror(fetch_status));
		goto movie_detail_error;
	}
	curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_resp_code);
	if (http_resp_code != 200)
	{
		DPRINTF(E_WARN, L_HTTP, "External metadata details response code: %ld\n", http_resp_code);
		if (http_resp_code == 429)
		{
			parse_and_wait_until(resp_hdrs);
			curl_slist_free_all(resp_hdrs);
			memset(http_data, 0, write_response.pos);
			write_response.pos = 0;
			goto http_movie_details_request;
		}
	}
	http_data[write_response.pos] = '\0';
	DPRINTF(E_DEBUG, L_METADATA, "External metadata details response: %s\n", http_data);

	js_root = json_loads(http_data, 0, &js_err);
	if (!js_root)
	{
		DPRINTF(E_ERROR, L_METADATA, "JSON conversion failed: [%d] %s\n", js_err.line, js_err.text);
		goto movie_detail_error;
	}
	if (!json_is_object(js_root))
	{
		goto movie_detail_error;
	}
	json_t *object = json_object_get(js_root, "tagline");
	if (object && json_is_string(object) && json_string_length(object))
	{
		meta->comment = strdup(json_string_value(object));
		*metaflags |= FLAG_COMMENT;
	}
	object = json_object_get(js_root, "vote_average");
	if (object && json_is_real(object))
	{
		snprintf(helper, 64, "%f/10", json_real_value(object));
		meta->rating = strdup(helper);
		*metaflags |= FLAG_RATING;
	}
	object = json_object_get(js_root, "genres");
	if (object && json_is_array(object) && json_array_size(object))
	{
		memset(helper, 0, sizeof(helper));
		size_t len = json_array_size(object);
		for (size_t gpos = 0; gpos < len; gpos++)
		{
			json_t *record = json_array_get(object, gpos);
			json_t *genre_name = json_object_get(record, "name");
			if (genre_name && json_is_string(genre_name) && json_string_length(genre_name))
			{
				x_strlcat(helper, json_string_value(genre_name), sizeof(helper));
				x_strlcat(helper, ",", sizeof(helper));
			}
		}
		helper[63] = '\0';
		len = strlen(helper);
		if (len > 0)
		{
			if (helper[len - 1] == ',') helper[len - 1] = '\0';
			meta->genre = strdup(helper);
			*metaflags |= FLAG_GENRE;
		}
	}
	object = json_object_get(js_root, "poster_path");
	if (object && json_is_string(object) && json_string_length(object))
	{
		struct album_art_name_s *art = calloc(1, sizeof(struct album_art_name_s));
		art->name = strdup(json_string_value(object));
		art->wildcard = FLAG_ART_TYPE_POSTER;
		if (*artwork)
		{
			struct album_art_name_s *a = *artwork;
			while (a->next)
				a = a->next;
			a->next = art;
		}
		else *artwork = art;
	}
	object = json_object_get(js_root, "backdrop_path");
	if (object && json_is_string(object) && json_string_length(object))
	{
		struct album_art_name_s *art = calloc(1, sizeof(struct album_art_name_s));
		art->name = strdup(json_string_value(object));
		art->wildcard = FLAG_ART_TYPE_BACKDROP;
		if (*artwork)
		{
			struct album_art_name_s *a = *artwork;
			while (a->next)
				a = a->next;
			a->next = art;
		}
		else *artwork = art;
	}
	if (!(*metaflags & FLAG_DATE))
	{
		object = json_object_get(js_root, "release_date");
		if (object && json_is_string(object) && json_string_length(object))
		{
			meta->date = strdup(json_string_value(object));
			*metaflags |= FLAG_DATE;
		}
	}
	ret = 1;
	goto movie_detail_cleanup;

movie_detail_error:
	DPRINTF(E_ERROR, L_METADATA, "Failed to parse external metadata response.\n");

movie_detail_cleanup:
	if (http_data) free(http_data);
	if (resp_hdrs) curl_slist_free_all(resp_hdrs);
	if (js_root) json_decref(js_root);
	return ret;
}

static int64_t
movie_db_get_movie_credits(CURL *conn, int64_t movie_id, metadata_t *meta, uint32_t *metaflags)
{
	CURLcode fetch_status;
	long http_resp_code;
	char *http_data = NULL;
	struct curl_slist *resp_hdrs = NULL;
	char query[MAXPATHLEN];
	int64_t ret = 0;
	json_t *js_root = NULL;
	json_error_t js_err;
	char helper[64];

	memset(query, 0, sizeof(query));
	memset(helper, 0, sizeof(helper));

	http_data = malloc(URL_MEDIUM_BUFFER_SZ);
	if (!http_data)
		return 0;
	struct http_response write_response = {
		.data = http_data,
		.pos = 0,
		.max_size = URL_MEDIUM_BUFFER_SZ
	};
	curl_easy_setopt(conn, CURLOPT_WRITEDATA, &write_response);
	curl_easy_setopt(conn, CURLOPT_HEADERDATA, resp_hdrs);

	/* compile query */
	snprintf(query, MAXPATHLEN, URL_MOVIE_DB_CREDIT_MOVIES, movie_id, the_moviedb_api_key);
	DPRINTF(E_DEBUG, L_HTTP, "External metadata credits URL: %s\n", query);
	curl_easy_setopt(conn, CURLOPT_URL, query);

http_movie_credits_request:
	resp_hdrs = curl_slist_append(resp_hdrs, ""); // initialize
	fetch_status = curl_easy_perform(conn);
	if (fetch_status != CURLE_OK)
	{
		DPRINTF(E_ERROR, L_HTTP, "External metadata credits error: %s\n", curl_easy_strerror(fetch_status));
		goto movie_credits_error;
	}
	curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_resp_code);
	if (http_resp_code != 200)
	{
		DPRINTF(E_WARN, L_HTTP, "External metadata credits response: %ld\n", http_resp_code);
		if (http_resp_code == 429)
		{
			parse_and_wait_until(resp_hdrs);
			curl_slist_free_all(resp_hdrs);
			memset(http_data, 0, write_response.pos);
			write_response.pos = 0;
			goto http_movie_credits_request;
		}
	}
	http_data[write_response.pos] = '\0';
	DPRINTF(E_DEBUG, L_METADATA, "External metadata credits response: %s\n", http_data);

	js_root = json_loads(http_data, 0, &js_err);
	if (!js_root)
	{
		DPRINTF(E_ERROR, L_METADATA, "JSON conversion failed: [%d] %s\n", js_err.line, js_err.text);
		goto movie_credits_error;
	}
	if (!json_is_object(js_root))
	{
		goto movie_credits_error;
	}

	json_t *cast = json_object_get(js_root, "cast");
	if (cast && json_is_array(cast) && json_array_size(cast))
	{
		char movie_cast[1024] = {0};
		size_t len = json_array_size(cast);
		for (size_t current = 0; current < len; current++)
		{
			json_t *actor = json_array_get(cast, current);
			json_t *actor_name = json_object_get(actor, "name");
			if (actor_name && json_is_string(actor_name) && json_string_length(actor_name))
			{
				x_strlcat(movie_cast, json_string_value(actor_name), 1024);
				x_strlcat(movie_cast, ",", 1024);
			}
		}
		movie_cast[1023] = '\0';
		/* do not end with a comma */
		len = strlen(movie_cast);
		if (len > 0)
		{
			if (movie_cast[len - 1] == ',') movie_cast[len - 1] = '\0';
			meta->artist = strdup(movie_cast);
			*metaflags |= FLAG_ARTIST;
		}
	}
	json_t *crew = json_object_get(js_root, "crew");
	if (crew && json_is_array(crew) && json_array_size(crew))
	{
		char movie_producers[255] = {0};
		char movie_directors[255] = {0};
		char movie_writers[255] = {0};
		size_t len = json_array_size(crew);
		for (size_t next = 0; next < len; next++)
		{
			json_t *contrib = json_array_get(crew, next);
			json_t *contrib_name = json_object_get(contrib, "name");
			json_t *job = json_object_get(contrib, "department");
			if (json_is_string(contrib_name) && json_string_length(contrib_name) && json_is_string(job) && json_string_length(job))
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
		len = strlen(movie_producers);
		if (len > 0)
		{
			if (movie_producers[len - 1] == ',') movie_producers[len - 1] = '\0';
			meta->creator = strdup(movie_producers);
			*metaflags |= FLAG_CREATOR;
		}
		len = strlen(movie_directors);
		if (len > 0)
		{
			if (movie_directors[len - 1] == ',') movie_directors[len - 1] = '\0';
			meta->director = strdup(movie_directors);
			*metaflags |= FLAG_DIRECTOR;
		}
		len = strlen(movie_writers);
		if (len > 0)
		{
			if (movie_writers[len - 1] == ',') movie_writers[len - 1] = '\0';
			meta->author = strdup(movie_writers);
			*metaflags |= FLAG_AUTHOR;
		}
	}

	ret = 1;
	goto movie_credits_cleanup;

movie_credits_error:
	DPRINTF(E_ERROR, L_METADATA, "Failed to parse external metadata response.\n");

movie_credits_cleanup:
	if (http_data) free(http_data);
	if (resp_hdrs) curl_slist_free_all(resp_hdrs);
	if (js_root) json_decref(js_root);
	return ret;
}

static i64
movie_db_search_tv(CURL *conn, const char *full_search, const char *show_search, int year_search,
		   metadata_t *meta_data, uint32_t *meta_flags)
{
#define TV_SEARCH_BASIC		0x01
#define TV_SEARCH_BASIC_YEAR	0x02
#define TV_SEARCH_SPECIFIC	0x04
#define TV_SEARCH_SPECIFIC_YEAR	0x08
#define TV_RESULT_BASIC		0x10
#define TV_RESULT_BASIC_YEAR	0x20
#define TV_RESULT_SPECIFIC	0x40
#define TV_RESULT_SPECIFIC_YEAR	0x80

	CURLcode fetch_status;
	long http_resp_code;
	char *http_data = NULL;
	struct curl_slist *resp_hdrs = NULL;
	char query[MAXPATHLEN];
	char *helper;
	i64 ret = 0;
	int search_type;
	json_t *js_root = NULL;
	json_error_t js_err;

	static char *last_show_found = NULL;
	static char *last_show_air_date = NULL;
	static int   last_show_year = 0;
	static i64   last_show_id = 0;

	/* check if the show has been found in a previous call */
	if (last_show_found && strlen(last_show_found))
	{
		if (show_search && strlen(show_search))
		{
			if (strcasecmp(show_search, last_show_found) == 0)
			{
				DPRINTF(E_DEBUG, L_METADATA, "Reusing a previously found show %s which matches search criteria %s\n",
					last_show_found, show_search);
				goto tv_search_process;
			}
		}
		else if (full_search && strlen(full_search))
		{
			if (strcasestr(full_search, last_show_found) != NULL)
			{
				DPRINTF(E_DEBUG, L_METADATA, "Reusing a previously found show %s which matches search criteria %s\n",
					last_show_found, full_search);
				goto tv_search_process;
			}
		}
	}

	/* prepare for a new search */
	memset(query, 0, sizeof(query));

	http_data = malloc(URL_SMALL_BUFFER_SZ);
	if (!http_data)
		return 0;
	struct http_response write_response = {
		.data = http_data,
		.pos = 0,
		.max_size = URL_SMALL_BUFFER_SZ
	};
	curl_easy_setopt(conn, CURLOPT_WRITEDATA, &write_response);
	curl_easy_setopt(conn, CURLOPT_HEADERDATA, resp_hdrs);

	/* compile query */
	if (show_search && strlen(show_search))
	{
		helper = curl_easy_escape(conn, show_search, 0);
		search_type = TV_SEARCH_SPECIFIC;
	}
	else if (full_search && strlen(full_search))
	{
		helper = curl_easy_escape(conn, full_search, 0);
		search_type = TV_SEARCH_BASIC;
	}
	else goto tv_search_cleanup;
	snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW, the_moviedb_api_key, helper);
	curl_free(helper);
	if (year_search)
	{
		helper = strdup(query);
		snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW_YEAR, helper, year_search);
		search_type <<= 1;
		free(helper);
	}
	DPRINTF(E_DEBUG, L_HTTP, "External metadata search URL: %s\n", query);
	curl_easy_setopt(conn, CURLOPT_URL, query);

tv_search_request:
	resp_hdrs = curl_slist_append(resp_hdrs, ""); // initialize
	fetch_status = curl_easy_perform(conn);
	if (fetch_status != CURLE_OK)
	{
		DPRINTF(E_ERROR, L_HTTP, "External metadata search error: %s\n", curl_easy_strerror(fetch_status));
		goto tv_search_error;
	}
	curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_resp_code);
	if (http_resp_code != 200)
	{
		DPRINTF(E_WARN, L_HTTP, "External metadata search response code: %ld\n", http_resp_code);
		if (http_resp_code == 429)
		{
			parse_and_wait_until(resp_hdrs);
			curl_slist_free_all(resp_hdrs);
			memset(http_data, 0, write_response.pos);
			write_response.pos = 0;
			goto tv_search_request;
		}
	}
	http_data[write_response.pos] = '\0';
	DPRINTF(E_DEBUG, L_METADATA, "External metadata search response: %s\n", http_data);

	js_root = json_loads(http_data, 0, &js_err);
	if (!js_root)
	{
		DPRINTF(E_ERROR, L_METADATA, "JSON conversion failed: [%d] %s\n", js_err.line, js_err.text);
		goto tv_search_error;
	}
	if (!json_is_object(js_root))
	{
		goto tv_search_error;
	}
	json_t *total_results = json_object_get(js_root, "total_results");
	json_t *total_pages = json_object_get(js_root, "total_pages");
	json_t *page = json_object_get(js_root, "page");
	if (!json_is_integer(total_results) && !json_is_integer(total_pages) && !json_is_integer(page))
	{
		goto tv_search_error;
	}
	json_int_t db_total_results = json_integer_value(total_results);
	if (db_total_results == 0)
	{
		// reset
		json_decref(js_root);
		memset(http_data, 0, write_response.pos);
		write_response.pos = 0;
		curl_slist_free_all(resp_hdrs);
		// try a different search
		if (search_type & TV_SEARCH_SPECIFIC_YEAR)
		{
			search_type = (search_type & ~TV_SEARCH_SPECIFIC_YEAR) | TV_RESULT_SPECIFIC_YEAR;
			if (!(search_type & TV_RESULT_SPECIFIC))
			{
				search_type |= TV_SEARCH_SPECIFIC;
				helper = curl_easy_escape(conn, show_search, 0);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW, the_moviedb_api_key, helper);
				curl_free(helper);
				curl_easy_setopt(conn, CURLOPT_URL, query);
				DPRINTF(E_DEBUG, L_HTTP, "Trying metadata search with: %s\n", query);
				goto tv_search_request;
			}
			else if (!(search_type & TV_RESULT_BASIC_YEAR) && full_search && strlen(full_search) && year_search)
			{
				search_type |= TV_SEARCH_BASIC_YEAR;
				helper = curl_easy_escape(conn, full_search, 0);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW, the_moviedb_api_key, helper);
				curl_free(helper);
				helper = strdup(query);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW_YEAR, helper, year_search);
				free(helper);
				curl_easy_setopt(conn, CURLOPT_URL, query);
				DPRINTF(E_DEBUG, L_HTTP, "Trying metadata search with: %s\n", query);
				goto tv_search_request;
			}
			else if (!(search_type & TV_RESULT_BASIC) && full_search && strlen(full_search))
			{
				search_type |= TV_SEARCH_BASIC;
				helper = curl_easy_escape(conn, full_search, 0);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW, the_moviedb_api_key, helper);
				curl_free(helper);
				curl_easy_setopt(conn, CURLOPT_URL, query);
				DPRINTF(E_DEBUG, L_HTTP, "Trying metadata search with: %s\n", query);
				goto tv_search_request;
			}
			else goto tv_search_cleanup; // nothing else left to try
		}
		else if (search_type & TV_SEARCH_SPECIFIC)
		{
			search_type = (search_type & ~TV_SEARCH_SPECIFIC) | TV_RESULT_SPECIFIC;
			if (!(search_type & TV_RESULT_SPECIFIC_YEAR) && year_search)
			{
				search_type |= TV_SEARCH_SPECIFIC;
				helper = curl_easy_escape(conn, show_search, 0);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW, the_moviedb_api_key, helper);
				curl_free(helper);
				helper = strdup(query);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW_YEAR, helper, year_search);
				free(helper);
				curl_easy_setopt(conn, CURLOPT_URL, query);
				DPRINTF(E_DEBUG, L_HTTP, "Trying metadata search with: %s\n", query);
				goto tv_search_request;
			}
			else if (!(search_type & TV_RESULT_BASIC_YEAR) && full_search && strlen(full_search) && year_search)
			{
				search_type |= TV_SEARCH_BASIC_YEAR;
				helper = curl_easy_escape(conn, full_search, 0);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW, the_moviedb_api_key, helper);
				curl_free(helper);
				helper = strdup(query);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW_YEAR, helper, year_search);
				free(helper);
				curl_easy_setopt(conn, CURLOPT_URL, query);
				DPRINTF(E_DEBUG, L_HTTP, "Trying metadata search with: %s\n", query);
				goto tv_search_request;
			}
			else if (!(search_type & TV_RESULT_BASIC) && full_search && strlen(full_search))
			{
				search_type |= TV_SEARCH_BASIC;
				helper = curl_easy_escape(conn, full_search, 0);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW, the_moviedb_api_key, helper);
				curl_free(helper);
				curl_easy_setopt(conn, CURLOPT_URL, query);
				DPRINTF(E_DEBUG, L_HTTP, "Trying metadata search with: %s\n", query);
				goto tv_search_request;
			}
			else goto tv_search_cleanup; // nothing else left to try
		}
		else if (search_type & TV_SEARCH_BASIC_YEAR)
		{
			search_type = (search_type & ~TV_SEARCH_BASIC_YEAR) | TV_RESULT_BASIC_YEAR;
			if (!(search_type & TV_RESULT_SPECIFIC_YEAR) && show_search && strlen(show_search) && year_search)
			{
				search_type |= TV_SEARCH_SPECIFIC;
				helper = curl_easy_escape(conn, show_search, 0);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW, the_moviedb_api_key, helper);
				curl_free(helper);
				helper = strdup(query);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW_YEAR, helper, year_search);
				free(helper);
				curl_easy_setopt(conn, CURLOPT_URL, query);
				DPRINTF(E_DEBUG, L_HTTP, "Trying metadata search with: %s\n", query);
				goto tv_search_request;
			}
			else if (!(search_type & TV_RESULT_SPECIFIC) && show_search && strlen(show_search))
			{
				search_type |= TV_SEARCH_SPECIFIC;
				helper = curl_easy_escape(conn, show_search, 0);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW, the_moviedb_api_key, helper);
				curl_free(helper);
				curl_easy_setopt(conn, CURLOPT_URL, query);
				DPRINTF(E_DEBUG, L_HTTP, "Trying metadata search with: %s\n", query);
				goto tv_search_request;
			}
			else if (!(search_type & TV_RESULT_BASIC))
			{
				search_type |= TV_SEARCH_BASIC;
				helper = curl_easy_escape(conn, full_search, 0);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW, the_moviedb_api_key, helper);
				curl_free(helper);
				curl_easy_setopt(conn, CURLOPT_URL, query);
				DPRINTF(E_DEBUG, L_HTTP, "Trying metadata search with: %s\n", query);
				goto tv_search_request;
			}
			else goto tv_search_cleanup; // nothing else left to try
		}
		else if (search_type & TV_SEARCH_BASIC)
		{
			search_type = (search_type & ~TV_SEARCH_BASIC) | TV_RESULT_BASIC;
			if (!(search_type & TV_RESULT_SPECIFIC_YEAR) && show_search && strlen(show_search) && year_search)
			{
				search_type |= TV_SEARCH_SPECIFIC;
				helper = curl_easy_escape(conn, show_search, 0);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW, the_moviedb_api_key, helper);
				curl_free(helper);
				helper = strdup(query);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW_YEAR, helper, year_search);
				free(helper);
				curl_easy_setopt(conn, CURLOPT_URL, query);
				DPRINTF(E_DEBUG, L_HTTP, "Trying metadata search with: %s\n", query);
				goto tv_search_request;
			}
			else if (!(search_type & TV_RESULT_SPECIFIC) && show_search && strlen(show_search))
			{
				search_type |= TV_SEARCH_SPECIFIC;
				helper = curl_easy_escape(conn, show_search, 0);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW, the_moviedb_api_key, helper);
				curl_free(helper);
				curl_easy_setopt(conn, CURLOPT_URL, query);
				DPRINTF(E_DEBUG, L_HTTP, "Trying metadata search with: %s\n", query);
				goto tv_search_request;
			}
			else if (!(search_type & TV_RESULT_BASIC_YEAR) && year_search)
			{
				search_type |= TV_SEARCH_BASIC_YEAR;
				helper = curl_easy_escape(conn, full_search, 0);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW, the_moviedb_api_key, helper);
				curl_free(helper);
				helper = strdup(query);
				snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_TVSHOW_YEAR, helper, year_search);
				free(helper);
				curl_easy_setopt(conn, CURLOPT_URL, query);
				DPRINTF(E_DEBUG, L_HTTP, "Trying metadata search with: %s\n", query);
				goto tv_search_request;
			}
			else goto tv_search_cleanup; // nothing else left to try
		}
		else goto tv_search_cleanup; // nothing else left to try
	}
	else
	{
		json_int_t db_total_pages = json_integer_value(total_pages);
		json_int_t db_page = json_integer_value(page);
		json_t *results = json_object_get(js_root, "results");
		if (!json_is_array(results))
			goto tv_search_error;
		size_t results_count = json_array_size(results);
		for (size_t next = 0; next < results_count; next++)
		{
			json_t *the_show = json_array_get(results, next);
			json_t *the_show_name = json_object_get(the_show, "name");
			if (!the_show_name || !json_is_string(the_show_name) || !json_string_length(the_show_name))
				continue;
			const char *show_name_str = json_string_value(the_show_name);
			if (!show_name_str || strlen(show_name_str) == 0)
				continue;
			const char *test = ((search_type & TV_SEARCH_SPECIFIC) || (search_type & TV_SEARCH_SPECIFIC_YEAR))
					  ? show_search : full_search;
			if (strcasecmp(show_name_str, test) == 0)
			{
				/* exact name match
				 * check if a year is supplied and it also matches the result */
				json_t *show_id = json_object_get(the_show, "id");
				if (!json_is_integer(show_id))
					continue; // unusable result, no way to get details, credits, etc. without id
				if (year_search)
				{
					json_t *res_year = json_object_get(the_show, "first_air_date");
					if (json_is_string(res_year) && json_string_length(res_year) > 0)
					{
						struct tm yr_rem;
						strptime(json_string_value(res_year), "%Y-%0m-%0d", &yr_rem);
						if (year_search == (yr_rem.tm_year + 1900))
						{
							if (last_show_air_date) free(last_show_air_date);
							if (last_show_found) free(last_show_found);
							last_show_found = strdup(show_name_str);
							last_show_year = year_search;
							last_show_air_date = strdup(json_string_value(res_year));
							last_show_id = json_integer_value(show_id);
							goto tv_search_process;
						}
						else
						{
							// name matches but asked for and provided years disagree: not a match
							continue;
						}
					}
					else
					{
						// year was not returned in result; probably a match
						if (last_show_air_date) free(last_show_air_date);
						if (last_show_found) free(last_show_found);
						last_show_year = 0;
						last_show_found = strdup(show_name_str);
						last_show_id = json_integer_value(show_id);
						goto tv_search_process;
					}
				}
				else
				{
					if (last_show_air_date) free(last_show_air_date);
					if (last_show_found) free(last_show_found);
					last_show_year = 0;
					last_show_found = strdup(show_name_str);
					last_show_id = json_integer_value(show_id);
					// is there an air date in the response?
					json_t *res_year = json_object_get(the_show, "first_air_date");
					if (json_is_string(res_year) && json_string_length(res_year) > 0)
					{
						last_show_air_date = strdup(json_string_value(res_year));
						struct tm yr_rem;
						strptime(json_string_value(res_year), "%Y-%0m-%0d", &yr_rem);
						last_show_year = (yr_rem.tm_year + 1900);
					}
					goto tv_search_process;
				}
			}
			// not a match, continue with results on current page
		}
		// no match on current page - are there more pages?
		if (db_page < db_total_pages)
		{
			helper = strdup(query);
			snprintf(query, MAXPATHLEN, URL_MOVIE_DB_SEARCH_NEXT_PAGE, helper, db_page + 1);
			free(helper);
			curl_easy_setopt(conn, CURLOPT_URL, query);
			DPRINTF(E_DEBUG, L_HTTP, "Getting next search result page with: %s\n", query);
			goto tv_search_request;
		}
	}
	// nothing found???
	goto tv_search_cleanup;

tv_search_process:
	DPRINTF(E_DEBUG, L_METADATA, "Found a matching show: %s [%s]\n", last_show_found, last_show_year ? last_show_air_date : "unknown year");
	meta_data->album = strdup(last_show_found);
	*meta_flags |= FLAG_ALBUM;
	if (last_show_year)
	{
		meta_data->original_date = strdup(last_show_air_date);
		*meta_flags |= FLAG_ORIG_DATE;
	}
	ret = last_show_id;
	goto tv_search_cleanup;

tv_search_error:
	DPRINTF(E_ERROR, L_METADATA, "Failed to parse external metadata search response.\n");

tv_search_cleanup:
	if (http_data) free(http_data);
	if (resp_hdrs) curl_slist_free_all(resp_hdrs);
	if (js_root) json_decref(js_root);
	return ret;
}

static int64_t
movie_db_get_tv_show_details(CURL *conn, int64_t show_id, metadata_t *meta, uint32_t *metaflags, struct album_art_name_s **artwork)
{
	CURLcode fetch_status;
	long http_resp_code;
	char *http_data = NULL;
	struct curl_slist *resp_hdrs = NULL;
	char query[MAXPATHLEN];
	int64_t ret = 0;
	json_t *js_root = NULL;
	json_error_t js_err;
	char helper[64];

	static int64_t last_detail_show_id = 0;
	static char *last_detail_genres = NULL;
	static char *last_detail_rating = NULL;
	static json_int_t last_detail_season_count = 0;
	static json_int_t last_detail_episode_count = 0;
	static struct album_art_name_s *last_detail_img_paths = NULL;
	static char *last_detail_networks = NULL;
	static char *last_detail_creators = NULL;
	static char *last_detail_overview = NULL;
	static char *last_detail_orig_air = NULL;

	if (last_detail_show_id > 0)
	{
		if (last_detail_show_id == show_id)
		{
			DPRINTF(E_DEBUG, L_METADATA, "Reusing details information from last show [id=%ld].\n", show_id);
			goto tv_detail_process;
		}
		else
		{
			last_detail_show_id = 0;
			last_detail_season_count = 0;
			last_detail_episode_count = 0;
			if (last_detail_genres)
			{
				free(last_detail_genres);
				last_detail_genres = NULL;
			}
			if (last_detail_rating)
			{
				free(last_detail_rating);
				last_detail_rating = NULL;
			}
			if (last_detail_img_paths)
			{
				struct album_art_name_s *a, *b;
				a = last_detail_img_paths;
				while (a)
				{
					b = a->next;
					free(a->name);
					free(a);
					a = b;
				}
				last_detail_img_paths = NULL;
			}
			if (last_detail_networks)
			{
				free(last_detail_networks);
				last_detail_networks = NULL;
			}
			if (last_detail_creators)
			{
				free(last_detail_creators);
				last_detail_creators = NULL;
			}
			if (last_detail_overview)
			{
				free(last_detail_overview);
				last_detail_overview = NULL;
			}
			if (last_detail_orig_air)
			{
				free(last_detail_orig_air);
				last_detail_orig_air = NULL;
			}
		}
	}
	last_detail_show_id = show_id;

	memset(query, 0, sizeof(query));
	memset(helper, 0, sizeof(helper));

	http_data = malloc(URL_SMALL_BUFFER_SZ);
	if (!http_data)
		return 0;
	struct http_response write_response = {
		.data = http_data,
		.pos = 0,
		.max_size = URL_SMALL_BUFFER_SZ
	};
	curl_easy_setopt(conn, CURLOPT_WRITEDATA, &write_response);
	curl_easy_setopt(conn, CURLOPT_HEADERDATA, resp_hdrs);

	/* compile query */
	snprintf(query, MAXPATHLEN, URL_MOVIE_DB_DETAIL_TVSHOW, show_id, the_moviedb_api_key);
	DPRINTF(E_DEBUG, L_HTTP, "External metadata detail URL: %s\n", query);
	curl_easy_setopt(conn, CURLOPT_URL, query);

http_tv_details_request:
	resp_hdrs = curl_slist_append(resp_hdrs, ""); // initialize
	fetch_status = curl_easy_perform(conn);
	if (fetch_status != CURLE_OK)
	{
		DPRINTF(E_ERROR, L_HTTP, "External metadata detail error: %s\n", curl_easy_strerror(fetch_status));
		goto tv_detail_error;
	}
	curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_resp_code);
	if (http_resp_code != 200)
	{
		DPRINTF(E_WARN, L_HTTP, "External metadata details response code: %ld\n", http_resp_code);
		if (http_resp_code == 429)
		{
			parse_and_wait_until(resp_hdrs);
			curl_slist_free_all(resp_hdrs);
			memset(http_data, 0, write_response.pos);
			write_response.pos = 0;
			goto http_tv_details_request;
		}
	}
	http_data[write_response.pos] = '\0';
	DPRINTF(E_DEBUG, L_METADATA, "External metadata details response: %s\n", http_data);

	js_root = json_loads(http_data, 0, &js_err);
	if (!js_root)
	{
		DPRINTF(E_ERROR, L_METADATA, "JSON conversion failed: [%d] %s\n", js_err.line, js_err.text);
		goto tv_detail_error;
	}
	/* backdrop image */
	json_t *backdrop = json_object_get(js_root, "backdrop_path");
	if (backdrop && json_is_string(backdrop) && json_string_length(backdrop))
	{
//		DPRINTF(E_DEBUG, L_HTTP, "Received backdrop_path in TV show detail response for %s\n", meta->album);
		struct album_art_name_s *art = calloc(1, sizeof(struct album_art_name_s));
		art->wildcard = FLAG_ART_TYPE_BACKDROP;
		art->name = strdup(json_string_value(backdrop));
		if (last_detail_img_paths)
		{
			struct album_art_name_s *n = last_detail_img_paths;
			while (n->next)
				n = n->next;
			n->next = art;
		}
		else last_detail_img_paths = art;
	}
	/* creators */
	json_t *creators = json_object_get(js_root, "created_by");
	if (creators && json_is_array(creators) && json_array_size(creators))
	{
//		DPRINTF(E_DEBUG, L_HTTP, "Received created_by in TV show detail response for %s\n", meta->album);
		size_t len = json_array_size(creators);
		memset(helper, 0, 64);
		for (size_t next = 0; next < len; next++)
		{
			json_t *person = json_array_get(creators, next);
			json_t *name = json_object_get(person, "name");
			if (name && json_is_string(name) && json_string_length(name))
			{
				x_strlcat(helper, json_string_value(name), 64);
				x_strlcat(helper, ",", 64);
			}
		}
		// make sure string is terminated and does not end with ','
		helper[63] = '\0';
		len = strlen(helper);
		if (len)
		{
			if (helper[len - 1] == ',') helper[len - 1] = '\0';
			last_detail_creators = strdup(helper);
		}
	}
	/* first air date (original_date) */
	json_t *first_aired = json_object_get(js_root, "first_air_date");
	if (first_aired && json_is_string(first_aired) && json_string_length(first_aired))
	{
//		DPRINTF(E_DEBUG, L_HTTP, "Received first_air_date in TV show detail response for %s\n", meta->album);
		last_detail_orig_air = strdup(json_string_value(first_aired));
	}
	/* genres */
	json_t *genres = json_object_get(js_root, "genres");
	if (genres && json_is_array(genres) && json_array_size(genres))
	{
//		DPRINTF(E_DEBUG, L_HTTP, "Received genres in TV show detail response for %s\n", meta->album);
		size_t total = json_array_size(genres);
		memset(helper, 0, 64);
		for (size_t idx = 0; idx < total; idx++)
		{
			json_t *g = json_array_get(genres, idx);
			json_t *g_name = json_object_get(g, "name");
			if (g_name && json_is_string(g_name))
			{
				x_strlcat(helper, json_string_value(g_name), 64);
				x_strlcat(helper, ",", 64);
			}
		}
		// make sure string is terminated and it does not end with separator "|"
		helper[63] = '\0';
		size_t len = strlen(helper);
		if (len)
		{
			if (helper[len - 1] == ',') helper[len - 1] = '\0';
			last_detail_genres = strdup(helper);
//			DPRINTF(E_DEBUG, L_HTTP, "Got TV show genres for %s: %s\n", meta->album, last_detail_genres);
		}
	}
	/* networks (publisher) */
	json_t *networks = json_object_get(js_root, "networks");
	if (networks && json_is_array(networks) && json_array_size(networks))
	{
//		DPRINTF(E_DEBUG, L_HTTP, "Received networks in TV show detail response for %s\n", meta->album);
		size_t len = json_array_size(networks);
		memset(helper, 0, 64);
		for (size_t n = 0; n < len; n++)
		{
			json_t *net = json_array_get(networks, n);
			json_t *name = json_object_get(net, "name");
			if (name && json_is_string(name) && json_string_length(name))
			{
				x_strlcat(helper, json_string_value(name), 64);
				x_strlcat(helper, ",", 64);
			}
		}
		// make sure string is terminated and does not end with ','
		helper[63] = '\0';
		len = strlen(helper);
		if (len)
		{
			if (helper[len - 1] == ',') helper[len - 1] = '\0';
			last_detail_networks = strdup(helper);
		}
	}
	/* total # episodes */
	json_t *episodes = json_object_get(js_root, "number_of_episodes");
	if (episodes && json_is_integer(episodes))
	{
//		DPRINTF(E_DEBUG, L_HTTP, "Received number_of_episodes in TV show detail response for %s\n", meta->album);
		last_detail_episode_count = json_integer_value(episodes);
	}
	/* total # seasons */
	json_t *seasons = json_object_get(js_root, "number_of_seasons");
	if (seasons && json_is_integer(seasons))
	{
//		DPRINTF(E_DEBUG, L_HTTP, "Received number_of_seasons in TV show detail response for %s\n", meta->album);
		last_detail_season_count = json_integer_value(seasons);
	}
	/* overview (orig description) */
	json_t *overview = json_object_get(js_root, "overview");
	if (overview && json_is_string(overview) && json_string_length(overview))
	{
//		DPRINTF(E_DEBUG, L_HTTP, "Received overview in TV show detail response for %s\n", meta->album);
		last_detail_overview = strdup(json_string_value(overview));
	}
	/* poster image */
	json_t *poster = json_object_get(js_root, "poster_path");
	if (poster && json_is_string(poster) && json_string_length(poster))
	{
//		DPRINTF(E_DEBUG, L_HTTP, "Received poster_path in TV show detail response for %s\n", meta->album);
		struct album_art_name_s *art = calloc(1, sizeof(struct album_art_name_s));
		art->wildcard = FLAG_ART_TYPE_BACKDROP;
		art->name = strdup(json_string_value(poster));
		if (last_detail_img_paths)
		{
			struct album_art_name_s *n = last_detail_img_paths;
			while (n->next)
				n = n->next;
			n->next = art;
		}
		else last_detail_img_paths = art;
	}
	/* orig rating */
	json_t *rating = json_object_get(js_root, "vote_average");
	if (rating && json_is_real(rating))
	{
//		DPRINTF(E_DEBUG, L_HTTP, "Received vote_average in TV show detail response for %s\n", meta->album);
		char rate[10] = { 0 };
		snprintf(rate, 10, "%.1f/10", json_real_value(rating));
		last_detail_rating = strdup(rate);
	}
	else if (rating && json_is_integer(rating))
	{
//		DPRINTF(E_DEBUG, L_HTTP, "Received vote_average in TV show detail response for %s\n", meta->album);
		char rate[10] = { 0 };
		snprintf(rate, 10, "%lld.0/10", json_integer_value(rating));
		last_detail_rating = strdup(rate);
	}

tv_detail_process:
	if (last_detail_genres && strlen(last_detail_genres))
	{
//		DPRINTF(E_DEBUG, L_METADATA, "Setting TV show genres for %s: %s\n", meta->album, last_detail_genres);
		meta->genre = strdup(last_detail_genres);
		*metaflags |= FLAG_GENRE;
	}
	if (last_detail_season_count)
	{
		meta->original_disc = last_detail_season_count;
		*metaflags |= FLAG_ORIG_DISC;
	}
	if (last_detail_episode_count)
	{
		meta->original_track = last_detail_episode_count;
		*metaflags |= FLAG_ORIG_TRACK;
	}
	if (last_detail_rating && strlen(last_detail_rating))
	{
		meta->original_rating = strdup(last_detail_rating);
		*metaflags |= FLAG_ORIG_RATING;
	}
	if (last_detail_networks)
	{
		meta->publisher = strdup(last_detail_networks);
		*metaflags |= FLAG_PUBLISHER;
	}
	if (last_detail_img_paths)
	{
		struct album_art_name_s *src, *tgt, *last;
		src = last_detail_img_paths;
		while (src)
		{
			tgt = calloc(1, sizeof(struct album_art_name_s));
			tgt->name = strdup(src->name);
			tgt->wildcard = src->wildcard;
			if (*artwork)
			{
				last = *artwork;
				while (last->next)
					last = last->next;
				last->next = tgt;
			}
			else *artwork = tgt;
			src = src->next;
		}
	}
	if (last_detail_creators)
	{
		meta->original_creator = strdup(last_detail_creators);
		*metaflags |= FLAG_ORIG_CREATOR;
	}
	if (last_detail_overview)
	{
		meta->original_description = strdup(last_detail_overview);
		*metaflags |= FLAG_ORIG_DESCRIPTION;
	}
	ret = 1;
	goto tv_detail_cleanup;

tv_detail_error:
	DPRINTF(E_ERROR, L_METADATA, "Failed to parse external metadata response.\n");

tv_detail_cleanup:
	if (http_data) free(http_data);
	if (resp_hdrs) curl_slist_free_all(resp_hdrs);
	if (js_root) json_decref(js_root);
	return ret;
}

static int64_t
movie_db_get_tv_episode_details(CURL *conn, int64_t show_id, int season, int episode, metadata_t *meta, uint32_t *metaflags, struct album_art_name_s **img_paths)
{
	CURLcode fetch_status;
	long http_resp_code;
	char *http_data = NULL;
	struct curl_slist *resp_hdrs = NULL;
	char query[MAXPATHLEN];
	int64_t ret = 0;
	json_t *js_root = NULL;
	json_error_t js_err;
	char helper[64];

	memset(query, 0, sizeof(query));
	memset(helper, 0, sizeof(helper));

	http_data = malloc(URL_MEDIUM_BUFFER_SZ);
	if (!http_data)
		return 0;
	struct http_response write_response = {
		.data = http_data,
		.pos = 0,
		.max_size = URL_MEDIUM_BUFFER_SZ
	};
	curl_easy_setopt(conn, CURLOPT_WRITEDATA, &write_response);
	curl_easy_setopt(conn, CURLOPT_HEADERDATA, resp_hdrs);

	/* compile query */
	snprintf(query, MAXPATHLEN, URL_MOVIE_DB_DETAIL_TVEPISODE, show_id, season, episode, the_moviedb_api_key);
	DPRINTF(E_DEBUG, L_HTTP, "External episode metadata credits URL: %s\n", query);
	curl_easy_setopt(conn, CURLOPT_URL, query);

http_tv_details_request:
	resp_hdrs = curl_slist_append(resp_hdrs, ""); // initialize
	fetch_status = curl_easy_perform(conn);
	if (fetch_status != CURLE_OK)
	{
		DPRINTF(E_ERROR, L_HTTP, "External metadata details error: %s\n", curl_easy_strerror(fetch_status));
		goto tv_details_error;
	}
	curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_resp_code);
	if (http_resp_code != 200)
	{
		DPRINTF(E_WARN, L_HTTP, "External metadata credits response: %ld\n", http_resp_code);
		if (http_resp_code == 429)
		{
			parse_and_wait_until(resp_hdrs);
			curl_slist_free_all(resp_hdrs);
			memset(http_data, 0, write_response.pos);
			write_response.pos = 0;
			goto http_tv_details_request;
		}
	}
	http_data[write_response.pos] = '\0';
	DPRINTF(E_DEBUG, L_METADATA, "External metadata details response: %s\n", http_data);

	js_root = json_loads(http_data, 0, &js_err);
	if (!js_root)
	{
		DPRINTF(E_ERROR, L_METADATA, "JSON conversion failed: [%d] %s\n", js_err.line, js_err.text);
		goto tv_details_error;
	}
	if (!json_is_object(js_root))
	{
		goto tv_details_error;
	}

	/* episode air date (date) */
	json_t *air_date = json_object_get(js_root, "air_date");
	if (air_date && json_is_string(air_date) && json_string_length(air_date))
	{
		meta->date = strdup(json_string_value(air_date));
		*metaflags |= FLAG_DATE;
	}
	/* cast (artists) */
	char episode_cast[1024] = {0};
	json_t *cast = json_object_get(js_root, "cast");
	if (cast && json_is_array(cast) && json_array_size(cast))
	{
		size_t len = json_array_size(cast);
		for (size_t current = 0; current < len; current++)
		{
			json_t *actor = json_array_get(cast, current);
			json_t *actor_name = json_object_get(actor, "name");
			if (actor_name && json_is_string(actor_name) && json_string_length(actor_name))
			{
				const char *person = json_string_value(actor_name);
				if (meta->artist && strcasestr(meta->artist, person))
					continue;
				x_strlcat(episode_cast, person, 1024);
				x_strlcat(episode_cast, ",", 1024);
			}
		}
	}
	cast = json_object_get(js_root, "guest_stars");
	if (cast && json_is_array(cast) && json_array_size(cast))
	{
		size_t len = json_array_size(cast);
		for (size_t current = 0; current < len; current++)
		{
			json_t *actor = json_array_get(cast, current);
			json_t *actor_name = json_object_get(actor, "name");
			if (actor_name && json_is_string(actor_name) && json_string_length(actor_name))
			{
				const char *person = json_string_value(actor_name);
				if (meta->artist && strcasestr(meta->artist, person))
					continue;
				x_strlcat(episode_cast, person, 1024);
				x_strlcat(episode_cast, ",", 1024);
			}
		}
	}
	episode_cast[1023] = '\0';
	/* do not end with a comma */
	size_t cast_len = strlen(episode_cast);
	if (cast_len > 0)
	{
		if (episode_cast[cast_len - 1] == ',') episode_cast[cast_len - 1] = '\0';
		if (!meta->artist)
			meta->artist = strdup(episode_cast);
		else
		{
			cast_len = strlen(meta->artist) + strlen(episode_cast) + 2;
			char *tmp = calloc(1, cast_len);
			if (tmp)
			{
				x_strlcat(tmp, meta->artist, cast_len);
				x_strlcat(tmp, ",", cast_len);
				x_strlcat(tmp, episode_cast, cast_len);
				free(meta->artist);
				meta->artist = strdup(tmp);
				free(tmp);
			}
		}
		*metaflags |= FLAG_ARTIST;
	}
	/* crew (authors, directors, creators) */
	json_t *crew = json_object_get(js_root, "crew");
	if (crew && json_is_array(crew) && json_array_size(crew))
	{
		char producers[255] = {0};
		char directors[255] = {0};
		char writers[255] = {0};
		size_t len = json_array_size(crew);
		for (size_t next = 0; next < len; next++)
		{
			json_t *contrib = json_array_get(crew, next);
			json_t *contrib_name = json_object_get(contrib, "name");
			json_t *job = json_object_get(contrib, "department");
			if (json_is_string(contrib_name) && json_string_length(contrib_name) && json_is_string(job) && json_string_length(job))
			{
				const char *person = json_string_value(contrib_name);
				if (strcasestr(json_string_value(job), "Directing"))
				{
					if (meta->director && strcasestr(meta->director, person))
						continue;
					x_strlcat(directors, person, 255);
					x_strlcat(directors, ",", 255);
				}
				else if (strcasestr(json_string_value(job), "Writing"))
				{
					if (meta->author && strcasestr(meta->author, person))
						continue;
					x_strlcat(writers, person, 255);
					x_strlcat(writers, ",", 255);
				}
				else if (strcasestr(json_string_value(job), "Production"))
				{
					if (meta->creator && strcasestr(meta->creator, person))
						continue;
					x_strlcat(producers, person, 255);
					x_strlcat(producers, ",", 255);
				}
			}
		}
		producers[254] = '\0';
		directors[254] = '\0';
		writers[254] = '\0';
		/* do not end with a comma */
		len = strlen(directors);
		if (len > 0)
		{
			if (directors[len - 1] == ',') directors[len - 1] = '\0';
			if (!meta->director)
				meta->director = strdup(directors);
			else
			{
				len = strlen(meta->director) + strlen(directors) + 2;
				char *tmp = calloc(1, len);
				if (tmp)
				{
					x_strlcat(tmp, meta->director, len);
					x_strlcat(tmp, ",", len);
					x_strlcat(tmp, directors, len);
					free(meta->director);
					meta->director = strdup(tmp);
					free(tmp);
				}
			}
			*metaflags |= FLAG_DIRECTOR;
		}
		len = strlen(producers);
		if (len > 0)
		{
			if (producers[len - 1] == ',') producers[len - 1] = '\0';
			if (!meta->creator)
				meta->creator = strdup(producers);
			else
			{
				len = strlen(meta->creator) + strlen(producers) + 2;
				char *tmp = calloc(1, len);
				if (tmp)
				{
					x_strlcat(tmp, meta->creator, len);
					x_strlcat(tmp, ",", len);
					x_strlcat(tmp, producers, len);
					free(meta->creator);
					meta->creator = strdup(tmp);
					free(tmp);
				}
			}
			*metaflags |= FLAG_CREATOR;
		}
		len = strlen(writers);
		if (len > 0)
		{
			if (writers[len - 1] == ',') writers[len - 1] = '\0';
			if (!meta->author)
				meta->author = strdup(writers);
			else
			{
				len = strlen(meta->author) + strlen(writers) + 2;
				char *tmp = calloc(1, len);
				if (tmp)
				{
					x_strlcat(tmp, meta->author, len);
					x_strlcat(tmp, ",", len);
					x_strlcat(tmp, writers, len);
					free(meta->author);
					meta->author = strdup(tmp);
					free(tmp);
				}
			}
			*metaflags |= FLAG_AUTHOR;
		}
	}
	/* episode # (track) */
	json_t *ep_number = json_object_get(js_root, "episode_number");
	if (ep_number && json_is_integer(ep_number))
	{
		meta->track = json_integer_value(ep_number);
		*metaflags |= FLAG_TRACK;
	}
	/* name (title) */
	json_t *ep_name = json_object_get(js_root, "name");
	if (ep_name && json_is_string(ep_name) && json_string_length(ep_name))
	{
		meta->title = strdup(json_string_value(ep_name));
		*metaflags |= FLAG_TITLE;
	}
	/* overview (description) */
	json_t *overview = json_object_get(js_root, "overview");
	if (overview && json_is_string(overview) && json_string_length(overview))
	{
		meta->description = strdup(json_string_value(overview));
		*metaflags |= FLAG_DESCRIPTION;
	}
	/* season # (disc) */
	json_t *season_number = json_object_get(js_root, "season_number");
	if (season_number && json_is_integer(season_number))
	{
		meta->disc = json_integer_value(season_number);
		*metaflags |= FLAG_DISC;
	}
	/* still image (artwork) */
	json_t *still = json_object_get(js_root, "still_path");
	if (still && json_is_string(still) && json_string_length(still))
	{
		struct album_art_name_s *art = calloc(1, sizeof(struct album_art_name_s));
		art->name = strdup(json_string_value(still));
		art->wildcard = FLAG_ART_TYPE_STILL;
		if (*img_paths)
		{
			struct album_art_name_s *n = *img_paths;
			while (n->next)
				n = n->next;
			n->next = art;
		}
		else *img_paths = art;
	}
	/* rating */
	json_t *rating = json_object_get(js_root, "vote_average");
	if (rating && json_is_real(rating))
	{
		char rate[10] = { 0 };
		snprintf(rate, 10, "%.1f/10", json_real_value(rating));
		meta->rating = strdup(rate);
		*metaflags |= FLAG_RATING;
	}
	else if (rating && json_is_integer(rating))
	{
		char rate[10] = { 0 };
		snprintf(rate, 10, "%lld.0/10", json_integer_value(rating));
		meta->rating = strdup(rate);
		*metaflags |= FLAG_RATING;
	}

	ret = 1;
	goto tv_details_cleanup;

tv_details_error:
	DPRINTF(E_ERROR, L_METADATA, "Failed to parse external metadata response.\n");

tv_details_cleanup:
	if (http_data) free(http_data);
	if (resp_hdrs) curl_slist_free_all(resp_hdrs);
	if (js_root) json_decref(js_root);
	return ret;
}

static int64_t
movie_db_get_tv_episode_credits(CURL *conn, int64_t show_id, int season, int episode, metadata_t *meta, uint32_t *metaflags)
{
	CURLcode fetch_status;
	long http_resp_code;
	char *http_data = NULL;
	struct curl_slist *resp_hdrs = NULL;
	char query[MAXPATHLEN];
	int64_t ret = 0;
	json_t *js_root = NULL;
	json_error_t js_err;
	char helper[64];

	memset(query, 0, sizeof(query));
	memset(helper, 0, sizeof(helper));

	http_data = malloc(URL_MEDIUM_BUFFER_SZ);
	if (!http_data)
		return 0;
	struct http_response write_response = {
		.data = http_data,
		.pos = 0,
		.max_size = URL_MEDIUM_BUFFER_SZ
	};
	curl_easy_setopt(conn, CURLOPT_WRITEDATA, &write_response);
	curl_easy_setopt(conn, CURLOPT_HEADERDATA, resp_hdrs);

	/* compile query */
	snprintf(query, MAXPATHLEN, URL_MOVIE_DB_CREDIT_TVEPISODE, show_id, season, episode, the_moviedb_api_key);
	DPRINTF(E_DEBUG, L_HTTP, "External episode metadata credits URL: %s\n", query);
	curl_easy_setopt(conn, CURLOPT_URL, query);

http_tv_credits_request:
	resp_hdrs = curl_slist_append(resp_hdrs, ""); // initialize
	fetch_status = curl_easy_perform(conn);
	if (fetch_status != CURLE_OK)
	{
		DPRINTF(E_ERROR, L_HTTP, "External metadata credits error: %s\n", curl_easy_strerror(fetch_status));
		goto tv_credits_error;
	}
	curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_resp_code);
	if (http_resp_code != 200)
	{
		DPRINTF(E_WARN, L_HTTP, "External metadata credits response: %ld\n", http_resp_code);
		if (http_resp_code == 429)
		{
			parse_and_wait_until(resp_hdrs);
			curl_slist_free_all(resp_hdrs);
			memset(http_data, 0, write_response.pos);
			write_response.pos = 0;
			goto http_tv_credits_request;
		}
	}
	http_data[write_response.pos] = '\0';
	DPRINTF(E_DEBUG, L_METADATA, "External metadata credits response: %s\n", http_data);

	js_root = json_loads(http_data, 0, &js_err);
	if (!js_root)
	{
		DPRINTF(E_ERROR, L_METADATA, "JSON conversion failed: [%d] %s\n", js_err.line, js_err.text);
		goto tv_credits_error;
	}
	if (!json_is_object(js_root))
	{
		goto tv_credits_error;
	}

	/* cast (artist) */
	char episode_cast[1024] = {0};
	json_t *cast = json_object_get(js_root, "cast");
	if (cast && json_is_array(cast) && json_array_size(cast))
	{
		size_t len = json_array_size(cast);
		for (size_t current = 0; current < len; current++)
		{
			json_t *actor = json_array_get(cast, current);
			json_t *actor_name = json_object_get(actor, "name");
			if (actor_name && json_is_string(actor_name) && json_string_length(actor_name))
			{
				const char *person = json_string_value(actor_name);
				if (meta->artist && strcasestr(meta->artist, person))
					continue;
				x_strlcat(episode_cast, person, 1024);
				x_strlcat(episode_cast, ",", 1024);
			}
		}
	}
	cast = json_object_get(js_root, "guest_stars");
	if (cast && json_is_array(cast) && json_array_size(cast))
	{
		size_t len = json_array_size(cast);
		for (size_t current = 0; current < len; current++)
		{
			json_t *actor = json_array_get(cast, current);
			json_t *actor_name = json_object_get(actor, "name");
			if (actor_name && json_is_string(actor_name) && json_string_length(actor_name))
			{
				const char *person = json_string_value(actor_name);
				if (meta->artist && strcasestr(meta->artist, person))
					continue;
				x_strlcat(episode_cast, person, 1024);
				x_strlcat(episode_cast, ",", 1024);
			}
		}
	}
	episode_cast[1023] = '\0';
	/* do not end with a comma */
	size_t cast_len = strlen(episode_cast);
	if (cast_len > 0)
	{
		if (episode_cast[cast_len - 1] == ',') episode_cast[cast_len - 1] = '\0';
		if (!meta->artist)
			meta->artist = strdup(episode_cast);
		else
		{
			cast_len = strlen(meta->artist) + strlen(episode_cast) + 2;
			char *tmp = calloc(1, cast_len);
			if (tmp)
			{
				x_strlcat(tmp, meta->artist, cast_len);
				x_strlcat(tmp, ",", cast_len);
				x_strlcat(tmp, episode_cast, cast_len);
				free(meta->artist);
				meta->artist = strdup(tmp);
				free(tmp);
			}
		}
		*metaflags |= FLAG_ARTIST;
	}
	/* crew (authors,directors,creators) */
	json_t *crew = json_object_get(js_root, "crew");
	if (crew && json_is_array(crew) && json_array_size(crew))
	{
		char producers[255] = {0};
		char directors[255] = {0};
		char writers[255] = {0};
		size_t len = json_array_size(crew);
		for (size_t next = 0; next < len; next++)
		{
			json_t *contrib = json_array_get(crew, next);
			json_t *contrib_name = json_object_get(contrib, "name");
			json_t *job = json_object_get(contrib, "department");
			if (json_is_string(contrib_name) && json_string_length(contrib_name) && json_is_string(job) && json_string_length(job))
			{
				const char *person = json_string_value(contrib_name);
				if (strcasestr(json_string_value(job), "Directing"))
				{
					if (meta->director && strcasestr(meta->director, person))
						continue;
					x_strlcat(directors, person, 255);
					x_strlcat(directors, ",", 255);
				}
				else if (strcasestr(json_string_value(job), "Writing"))
				{
					if (meta->author && strcasestr(meta->author, person))
						continue;
					x_strlcat(writers, person, 255);
					x_strlcat(writers, ",", 255);
				}
				else if (strcasestr(json_string_value(job), "Production"))
				{
					if (meta->creator && strcasestr(meta->creator, person))
						continue;
					x_strlcat(producers, person, 255);
					x_strlcat(producers, ",", 255);
				}
			}
		}
		producers[254] = '\0';
		directors[254] = '\0';
		writers[254] = '\0';
		/* do not end with a comma */
		len = strlen(directors);
		if (len > 0)
		{
			if (directors[len - 1] == ',') directors[len - 1] = '\0';
			if (!meta->director)
				meta->director = strdup(directors);
			else
			{
				len = strlen(meta->director) + strlen(directors) + 2;
				char *tmp = calloc(1, len);
				if (tmp)
				{
					x_strlcat(tmp, meta->director, len);
					x_strlcat(tmp, ",", len);
					x_strlcat(tmp, directors, len);
					free(meta->director);
					meta->director = strdup(tmp);
					free(tmp);
				}

			}
			*metaflags |= FLAG_DIRECTOR;
		}
		len = strlen(producers);
		if (len > 0)
		{
			if (producers[len - 1] == ',') producers[len - 1] = '\0';
			if (!meta->creator)
				meta->creator = strdup(producers);
			else
			{
				len = strlen(meta->creator) + strlen(producers) + 2;
				char *tmp = calloc(1, len);
				if (tmp)
				{
					x_strlcat(tmp, meta->creator, len);
					x_strlcat(tmp, ",", len);
					x_strlcat(tmp, producers, len);
					free(meta->creator);
					meta->creator = strdup(tmp);
					free(tmp);
				}
			}
			*metaflags |= FLAG_CREATOR;
		}
		len = strlen(writers);
		if (len > 0)
		{
			if (writers[len - 1] == ',') writers[len - 1] = '\0';
			if (!meta->author)
				meta->author = strdup(writers);
			else
			{
				len = strlen(meta->author) + strlen(writers) + 2;
				char *tmp = calloc(1, len);
				if (tmp)
				{
					x_strlcat(tmp, meta->author, len);
					x_strlcat(tmp, ",", len);
					x_strlcat(tmp, writers, len);
					free(meta->author);
					meta->author = strdup(tmp);
					free(tmp);
				}
			}
			*metaflags |= FLAG_AUTHOR;
		}
	}

	ret = 1;
	goto tv_credits_cleanup;

tv_credits_error:
	DPRINTF(E_ERROR, L_METADATA, "Failed to parse external metadata response.\n");

tv_credits_cleanup:
	if (http_data) free(http_data);
	if (resp_hdrs) curl_slist_free_all(resp_hdrs);
	if (js_root) json_decref(js_root);
	return ret;
}

static int64_t
movie_db_get_configuration(CURL *conn, char **base_url, struct album_art_name_s **art_sizes)
{
	static char *last_base_url = NULL;
	static struct album_art_name_s *last_img_sizes = NULL;
	CURLcode fetch_status;
	long http_resp_code;
	char *http_data = NULL;
	struct curl_slist *resp_hdrs = NULL;
	char query[MAXPATHLEN];
	int64_t ret = 0;
	json_t *js_root = NULL;
	json_error_t js_err;
	char helper[64];

	if (last_base_url && strlen(last_base_url) && last_img_sizes)
	{
		goto configuration_process;
	}

	memset(query, 0, sizeof(query));
	memset(helper, 0, sizeof(helper));

	http_data = malloc(URL_SMALL_BUFFER_SZ);
	if (!http_data)
		return 0;
	struct http_response write_response = {
		.data = http_data,
		.pos = 0,
		.max_size = URL_SMALL_BUFFER_SZ
	};
	curl_easy_setopt(conn, CURLOPT_WRITEDATA, &write_response);
	curl_easy_setopt(conn, CURLOPT_HEADERDATA, resp_hdrs);

	/* compile query */
	snprintf(query, MAXPATHLEN, URL_MOVIE_DB_CONFIGURATION, the_moviedb_api_key);
	DPRINTF(E_DEBUG, L_HTTP, "External metadata configuration URL: %s\n", query);
	curl_easy_setopt(conn, CURLOPT_URL, query);

make_config_request:
	resp_hdrs = curl_slist_append(resp_hdrs, ""); // initialize
	fetch_status = curl_easy_perform(conn);
	if (fetch_status != CURLE_OK)
	{
		DPRINTF(E_ERROR, L_HTTP, "External metadata configuration error code: %s\n", curl_easy_strerror(fetch_status));
		goto configuration_error;
	}
	curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_resp_code);
	if (http_resp_code != 200)
	{
		DPRINTF(E_WARN, L_HTTP, "External metadata configuration response code: %ld\n", http_resp_code);
		if (http_resp_code == 429) // request rate limiting
		{
			parse_and_wait_until(resp_hdrs);
			curl_slist_free_all(resp_hdrs);
			memset(http_data, 0, write_response.pos);
			write_response.pos = 0;
			goto make_config_request;
		}
	}
	http_data[write_response.pos] = '\0';
	DPRINTF(E_DEBUG, L_METADATA, "External metadata configuration response: %s\n", http_data);

	js_root = json_loads(http_data, 0, &js_err);
	if (!js_root)
	{
		DPRINTF(E_ERROR, L_METADATA, "JSON conversion failed: [%d] %s\n", js_err.line, js_err.text);
		goto configuration_error;
	}
	if (!json_is_object(js_root))
	{
		goto configuration_error;
	}
	json_t *images = json_object_get(js_root, "images");
	if (!json_is_object(images))
	{
		goto configuration_error;
	}
	json_t *object = json_object_get(images, "secure_base_url");
	if (!json_is_string(object))
	{
		goto configuration_error;
	}
	last_base_url = strdup(json_string_value(object));
	object = json_object_get(images, "poster_sizes");
	if (json_is_array(object))
	{
		size_t poster_size_count = json_array_size(object);
		for (size_t next = 0; next < poster_size_count; next++)
		{
			json_t *sz = json_array_get(object, next);
			if (sz && json_is_string(sz) && json_string_length(sz))
			{
				struct album_art_name_s *art = calloc(1, sizeof(struct album_art_name_s));
				art->name = strdup(json_string_value(sz));
				art->wildcard = FLAG_ART_TYPE_POSTER;
				if (last_img_sizes)
				{
					struct album_art_name_s *a = last_img_sizes;
					while (a->next)
						a = a->next;
					a->next = art;
				}
				else last_img_sizes = art;
			}
		}
	}
	object = json_object_get(images, "backdrop_sizes");
	if (json_is_array(object))
	{
		size_t poster_size_count = json_array_size(object);
		for (size_t next = 0; next < poster_size_count; next++)
		{
			json_t *sz = json_array_get(object, next);
			if (sz && json_is_string(sz) && json_string_length(sz))
			{
				struct album_art_name_s *art = calloc(1, sizeof(struct album_art_name_s));
				art->name = strdup(json_string_value(sz));
				art->wildcard = FLAG_ART_TYPE_BACKDROP;
				if (last_img_sizes)
				{
					struct album_art_name_s *a = last_img_sizes;
					while (a->next)
						a = a->next;
					a->next = art;
				}
				else last_img_sizes = art;
			}
		}
	}
	object = json_object_get(images, "still_sizes");
	if (json_is_array(object))
	{
		size_t poster_size_count = json_array_size(object);
		for (size_t next = 0; next < poster_size_count; next++)
		{
			json_t *sz = json_array_get(object, next);
			if (sz && json_is_string(sz) && json_string_length(sz))
			{
				struct album_art_name_s *art = calloc(1, sizeof(struct album_art_name_s));
				art->name = strdup(json_string_value(sz));
				art->wildcard = FLAG_ART_TYPE_STILL;
				if (last_img_sizes)
				{
					struct album_art_name_s *a = last_img_sizes;
					while (a->next)
						a = a->next;
					a->next = art;
				}
				else last_img_sizes = art;
			}
		}
	}

configuration_process:
	*base_url = strdup(last_base_url);
	struct album_art_name_s *src, *tgt;
	src = last_img_sizes;
	while (src)
	{
		struct album_art_name_s *art = calloc(1, sizeof(struct album_art_name_s));
		art->name = strdup(src->name);
		art->wildcard = src->wildcard;
		if (*art_sizes)
		{
			tgt = *art_sizes;
			while (tgt->next)
				tgt = tgt->next;
			tgt->next = art;
		}
		else *art_sizes = art;
		src = src->next;
	}
	ret = 1;

	goto configuration_cleanup;

configuration_error:
	DPRINTF(E_ERROR, L_METADATA, "Failed to parse external metadata response.\n");

configuration_cleanup:
	if (http_data) free(http_data);
	if (resp_hdrs) curl_slist_free_all(resp_hdrs);
	if (js_root) json_decref(js_root);
	return ret;
}

static size_t
movie_db_get_imageart(CURL *conn, const char *base_url, struct album_art_name_s * const img_size_sel, struct album_art_name_s * const img_art_paths, metadata_t *meta)
{
	CURLcode fetch_status;
	long http_resp_code;
	char *http_data = NULL;
	struct curl_slist *resp_hdrs = NULL;
	char query[MAXPATHLEN];
	size_t ret = 0;

	if (img_size_sel == NULL || img_art_paths == NULL || meta == NULL)
		return ret;

	memset(query, 0, sizeof(query));

	http_data = malloc(URL_HUGE_BUFFER_SZ);
	if (!http_data)
		return 0;
	struct http_response write_response = {
		.data = http_data,
		.pos = 0,
		.max_size = URL_HUGE_BUFFER_SZ
	};
	curl_easy_setopt(conn, CURLOPT_WRITEDATA, &write_response);
	curl_easy_setopt(conn, CURLOPT_HEADERDATA, resp_hdrs);

	struct album_art_name_s *sizes, *images;
	int counter = 0;
	for (sizes = img_size_sel; sizes != NULL; sizes = sizes->next)
	{
		for (images = img_art_paths; images != NULL; images = images->next)
		{
			if (sizes->wildcard != images->wildcard)
				continue; // not of the same image type
			if (resp_hdrs)
			{
				curl_slist_free_all(resp_hdrs);
				resp_hdrs = NULL;
			}
			if (http_data && write_response.pos)
			{
				memset(http_data, 0, write_response.pos);
			}
			write_response.pos = 0;

			/* compile query */
			snprintf(query, MAXPATHLEN, URL_MOVIE_DB_IMGART_MOVIES, base_url, sizes->name, images->name, the_moviedb_api_key);
			DPRINTF(E_DEBUG, L_HTTP, "External metadata imageart URL: %s\n", query);
			curl_easy_setopt(conn, CURLOPT_URL, query);

http_movie_imageart_request:
			resp_hdrs = curl_slist_append(resp_hdrs, ""); // initialize
			fetch_status = curl_easy_perform(conn);
			if (fetch_status != CURLE_OK)
			{
				DPRINTF(E_ERROR, L_HTTP, "External metadata imageart error: %s\n", curl_easy_strerror(fetch_status));
				if (counter == 0) goto movie_imgart_cleanup; // if this is the very first try, then bail
				else continue; // try next size
			}
			curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_resp_code);
			if (http_resp_code != 200)
			{
				DPRINTF(E_WARN, L_HTTP, "External metadata imageart response code: %ld\n", http_resp_code);
				if (http_resp_code == 429)
				{
					parse_and_wait_until(resp_hdrs);
					curl_slist_free_all(resp_hdrs);
					memset(http_data, 0, write_response.pos);
					write_response.pos = 0;
					goto http_movie_imageart_request;
				}
			}
			counter++;
			DPRINTF(E_DEBUG, L_HTTP, "Retrieved %ld bytes image art\n", write_response.pos);
			if (write_response.pos)
			{
				artwork_t *next_img = calloc(1, sizeof(artwork_t));
				next_img->thumb_type = sizes->wildcard;
				next_img->thumb_size = write_response.pos;
				next_img->thumb_data = malloc(write_response.pos);
				if (!next_img->thumb_data)
					goto movie_imgart_cleanup;
				memcpy(next_img->thumb_data, http_data, write_response.pos);
				if (meta->artwork)
				{
					artwork_t *n = meta->artwork;
					while (n->next)
						n = n->next;
					n->next = next_img;
				}
				else meta->artwork = next_img;
				ret += next_img->thumb_size;
			}
		}
		//break;
	}

movie_imgart_cleanup:
	if (http_data) free(http_data);
	if (resp_hdrs) curl_slist_free_all(resp_hdrs);
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
	char *show = NULL;
	int year = 0;
	int season = 0;
	int episode = 0;

	metadata_t m_data;
	uint32_t m_flags = FLAG_MIME|FLAG_DURATION|FLAG_DLNA_PN|FLAG_DATE;
	char *base_art_url = NULL;
	struct album_art_name_s *art_sizes = NULL;
	struct album_art_name_s *media_art = NULL;

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

	curl_easy_setopt(fetch, CURLOPT_WRITEFUNCTION, process_http_meta_response);
	curl_easy_setopt(fetch, CURLOPT_HEADERFUNCTION, process_http_response_headers);

	DPRINTF(E_DEBUG, L_METADATA, "Trying to find online metadata for %s.\n", name);

	if (is_tv)
	{
		clean_name = cleanup_name(name, "._");
		split_tv_name(clean_name, &show, &year, &season, &episode);
		result = movie_db_search_tv(fetch, clean_name, show, year, &m_data, &m_flags);
		if (result > 0)
		{
			movie_db_get_tv_show_details(fetch, result, &m_data, &m_flags, &media_art);
			/* some sanity checks */
			bool shall_continue = true;
			if (m_data.original_disc > 0)
			{
				if (season > m_data.original_disc) /* did not pick the correct show */
				{
					DPRINTF(E_WARN, L_METADATA, "Seems The Movie DB returned the wrong information: expected season %d to be part of %d total seasons.\n", season, m_data.original_disc);
					result = 0;
					shall_continue = false;
				}
			}
			if (shall_continue)
			{
				movie_db_get_tv_episode_details(fetch, result, season, episode, &m_data, &m_flags, &media_art);
				movie_db_get_tv_episode_credits(fetch, result, season, episode, &m_data, &m_flags);
				movie_db_get_configuration(fetch, &base_art_url, &art_sizes);
				movie_db_get_imageart(fetch, base_art_url, art_sizes, media_art, &m_data);
			}
		}
	}
	else
	{
		clean_name = cleanup_name(name, ".-_");
		year = strip_year(clean_name);

		result = movie_db_search_movies(fetch, clean_name, year, &m_data, &m_flags);
		if (result > 0)
		{
			movie_db_get_movie_details(fetch, result, &m_data, &m_flags, &media_art);
			movie_db_get_movie_credits(fetch, result, &m_data, &m_flags);
			movie_db_get_configuration(fetch, &base_art_url, &art_sizes);
			movie_db_get_imageart(fetch, base_art_url, art_sizes, media_art, &m_data);
		}
	}

	if (result > 0)
	{
		/* some housekeeping */
		if (!m_data.date && m_data.original_date)
		{
			m_data.date = strdup(m_data.original_date);
			m_flags |= FLAG_DATE;
		}
		if (!m_data.rating && m_data.original_rating)
		{
			m_data.rating = strdup(m_data.original_rating);
			m_flags |= FLAG_RATING;
		}
		if (!m_data.author && m_data.original_author)
		{
			m_data.author = strdup(m_data.original_author);
			m_flags |= FLAG_AUTHOR;
		}
		if (!m_data.creator && m_data.original_creator)
		{
			m_data.creator = strdup(m_data.original_creator);
			m_flags |= FLAG_CREATOR;
		}
		if (!m_data.description && m_data.original_description)
		{
			m_data.description = strdup(m_data.original_description);
			m_flags |= FLAG_DESCRIPTION;
		}
		int ret_sql;
		int64_t img_art = 0;
		if (m_data.artwork)
		{
//			img_art = find_album_art(path, m_data.thumb_data, m_data.thumb_size);
			// TODO: save any available artwork here and free the allocated memory when not needed anymore
			DPRINTF(E_DEBUG, L_ARTWORK, "Checking artwork for %s: %s\n", path,
				m_data.artwork->thumb_type == FLAG_ART_TYPE_POSTER ? "poster" :
				m_data.artwork->thumb_type == FLAG_ART_TYPE_BACKDROP ? "backdrop" :
				m_data.artwork->thumb_type == FLAG_ART_TYPE_STILL ? "still image" : "unknown"
			);
			void *img_data = malloc(m_data.artwork->thumb_size);
			if (img_data)
			{
				memcpy(img_data, m_data.artwork->thumb_data, m_data.artwork->thumb_size);
				*img = img_data;
				*img_sz = m_data.artwork->thumb_size;
			}
		}

		if (detailID)
		{
			ret_sql = sql_exec(db, "UPDATE DETAILS set TITLE = '%q', DATE = %Q, DESCRIPTION = %Q, GENRE = %Q, "
					   "COMMENT = %Q, ALBUM_ART = %lld, CREATOR = %Q, DIRECTOR = %Q, AUTHOR = %Q, DISC = %lld, "
					   "TRACK = %lld, ARTIST = %Q, PUBLISHER = %Q, RATING = %Q, ALBUM = '%q', ORIGINAL_DATE = %Q, "
					   "ORIGINAL_DISC = %lld, ORIGINAL_TRACK = %lld, ORIGINAL_RATING = %Q, ORIGINAL_DESCRIPTION = %Q "
					   "WHERE PATH=%Q and ID=%lld;",
					   m_data.title, m_data.date, m_data.description, m_data.genre, m_data.comment,
					   img_art, m_data.creator, m_data.director, m_data.author, m_data.disc, m_data.track,
					   m_data.artist, m_data.publisher, m_data.rating, m_data.album, m_data.original_date,
					   m_data.original_disc, m_data.original_track, m_data.original_rating,
					   m_data.original_description, path, detailID);
		}
		else
		{
			ret_sql = sql_exec(db, "INSERT into DETAILS"
					   " (PATH, SIZE, TIMESTAMP, DATE, TITLE, DESCRIPTION, MIME, COMMENT, GENRE, ALBUM_ART,"
					   " CREATOR, DIRECTOR, AUTHOR, ARTIST, PUBLISHER, RATING, DISC, TRACK, ALBUM,"
					   " ORIGINAL_DATE, ORIGINAL_DISC, ORIGINAL_TRACK, ORIGINAL_RATING, ORIGINAL_DESCRIPTION) "
					   "VALUES (%Q, %lld, %lld, %Q, '%q', %Q, '%q', %Q, %Q, %lld, %Q, %Q, %Q, %Q, %Q, %Q, "
						   "%lld, %lld, '%q', %Q, %lld, %lld, %Q, %Q);",
					   path, (long long)file.st_size, (long long)file.st_mtime, m_data.date, m_data.title,
					   m_data.description, m_data.mime, m_data.comment, m_data.genre, img_art, m_data.creator,
					   m_data.director, m_data.author, m_data.artist, m_data.publisher, m_data.rating,
					   m_data.album, m_data.original_date, m_data.original_disc, m_data.original_track,
					   m_data.original_rating, m_data.original_description);
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
	if (clean_name) free(clean_name);
	if (show) free(show);
	if (base_art_url) free(base_art_url);
	if (art_sizes != NULL)
	{
		struct album_art_name_s *next, *m = art_sizes;
		while (m)
		{
			free(m->name);
			next = m->next;
			free(m);
			m = next;
		}
		art_sizes = NULL;
	}
	if (media_art != NULL)
	{
		struct album_art_name_s *next, *m = media_art;
		while (m)
		{
			free(m->name);
			next = m->next;
			free(m);
			m = next;
		}
		media_art = NULL;
	}
	free_metadata(&m_data, m_flags);
	free_metadata_artwork(&m_data);

	return result;
}
