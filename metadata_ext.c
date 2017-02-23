//
// Created by ttodorov on 2/22/17.
//
#include <curl/curl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <jansson.h>
#include "upnpglobalvars.h"
#include "log.h"
#include "metadata.h"
#include "metadata_ext.h"
#include "utils.h"
#include "sql.h"

#define CURL_FETCH_BUFFER_SZ        	4096
#define CURL_MOVIEDB_SEARCH_MOVIES	"https://api.themoviedb.org/3/search/movie?api_key=%s&query=%s"
#define CURL_MOVIEDB_DETAIL_MOVIES	"https://api.themoviedb.org/3/movie/%ld?api_key=%s"
#define CURL_MOVIEDB_SEARCH_TVSHOW	"https://api.themoviedb.org/3/search/tv?api_key=%s&query=%s"
#define CURL_MOVIEDB_QUERY_WITH_YEAR    "%s&year=%s"

struct http_response
{
	char *data;
	int pos;
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
                DPRINTF(L_HTTP, E_WARN, "External metadata facility already initialized.\n");
                return;
        }

	if (strlen(the_moviedb_api_key) == 0)
	{
		DPRINTF(L_HTTP, E_ERROR, "API key for accessing external metadata is not configured.\n");
		return;
	}

	DPRINTF(L_HTTP, E_INFO, "Initializing external metadata facility...\n");

	curl_ver = curl_version_info(CURLVERSION_NOW);
	DPRINTF(L_HTTP, E_DEBUG, "Using libCURL version %s.\n", curl_ver->version);
	curl_global_init(CURL_GLOBAL_ALL);
	curl_is_initialized = true;
}

static size_t
ext_meta_response(void *ptr, size_t size, size_t nmemb, void *stream)
{
	struct http_response *result = (struct http_response *)stream;

	if(result->pos + size * nmemb >= CURL_FETCH_BUFFER_SZ - 1)
	{
		DPRINTF(L_HTTP, E_ERROR, "External metadata response buffer is too small (required %ld)\n", result->pos + size * nmemb);
		return 0;
	}

	memcpy(result->data + result->pos, ptr, size * nmemb);
	result->pos += size * nmemb;

	return size * nmemb;
}

static int64_t
moviedb_search_movies(const char *search_crit, const char *year, CURL *conn, metadata_t *meta, uint32_t *metaflags)
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

	http_data = malloc(CURL_FETCH_BUFFER_SZ);
	if (!http_data)
		return -1;
	struct http_response write_response = {
		.data = http_data,
		.pos = 0
	};
	curl_easy_setopt(conn, CURLOPT_WRITEDATA, &write_response);

	/* compile query */
	tmp_str = curl_easy_escape(conn, search_crit, 0);
	snprintf(query, MAXPATHLEN, CURL_MOVIEDB_SEARCH_MOVIES, the_moviedb_api_key, tmp_str);
	curl_free(tmp_str);
	if (year)
	{
		tmp_str = strdup(query);
		snprintf(query, MAXPATHLEN, CURL_MOVIEDB_QUERY_WITH_YEAR, tmp_str, year);
		free(tmp_str);
	}
	DPRINTF(L_HTTP, E_DEBUG, "External metadata search URL: %s\n", query);
	curl_easy_setopt(conn, CURLOPT_URL, query);

	fetch_status = curl_easy_perform(conn);
	if (fetch_status != 0)
	{
		DPRINTF(L_HTTP, E_ERROR, "External metadata search error: %s\n", curl_easy_strerror(fetch_status));
		ret = -2;
		goto movie_search_error;
	}
	curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_resp_code);
	if (http_resp_code != 200)
	{
		DPRINTF(L_HTTP, E_WARN, "External metadata search response: %ld\n", http_resp_code);
	}
	http_data[write_response.pos] = '\0';
	DPRINTF(L_METADATA, E_DEBUG, "External metadata search response: %s\n", http_data);

	js_root = json_loads(http_data, 0, &js_err);
	if (!js_root)
	{
		DPRINTF(L_METADATA, E_ERROR, "JSON conversion failed: [%d] %s\n", js_err.line, js_err.text);
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
	DPRINTF(L_METADATA, E_ERROR, "Failed to parse external metadata search response.\n");

movie_search_cleanup:
	if (http_data)
		free(http_data);
	if (js_root)
		json_decref(js_root);
	return ret;
}

static int64_t
moviedb_get_movie_details(int64_t movie_id, CURL *conn, metadata_t *meta, uint32_t *metaflags, char **artwork_path)
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

	http_data = malloc(CURL_FETCH_BUFFER_SZ);
	if (!http_data)
		return -1;
	struct http_response write_response = {
		.data = http_data,
		.pos = 0
	};
	curl_easy_setopt(conn, CURLOPT_WRITEDATA, &write_response);

	/* compile query */
	snprintf(query, MAXPATHLEN, CURL_MOVIEDB_DETAIL_MOVIES, movie_id, the_moviedb_api_key);
	DPRINTF(L_HTTP, E_DEBUG, "External metadata detail URL: %s\n", query);
	curl_easy_setopt(conn, CURLOPT_URL, query);

	fetch_status = curl_easy_perform(conn);
	if (fetch_status != 0)
	{
		DPRINTF(L_HTTP, E_ERROR, "External metadata detail error: %s\n", curl_easy_strerror(fetch_status));
		ret = -2;
		goto movie_detail_error;
	}
	curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_resp_code);
	if (http_resp_code != 200)
	{
		DPRINTF(L_HTTP, E_WARN, "External metadata details response: %ld\n", http_resp_code);
	}
	http_data[write_response.pos] = '\0';
	DPRINTF(L_METADATA, E_DEBUG, "External metadata details response: %s\n", http_data);

	js_root = json_loads(http_data, 0, &js_err);
	if (!js_root)
	{
		DPRINTF(L_METADATA, E_ERROR, "JSON conversion failed: [%d] %s\n", js_err.line, js_err.text);
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
	DPRINTF(L_METADATA, E_ERROR, "Failed to parse external metadata response.\n");

movie_detail_cleanup:
	if (http_data)
		free(http_data);
	if (js_root)
		json_decref(js_root);
	return ret;
}

int64_t
search_ext_meta(const char *path, char *name, int64_t detailID)
{
	int64_t result = 0;
	CURL *fetch = NULL;
	struct curl_slist *headers = NULL;

	struct stat file;
	char *clean_name = NULL;
	char *year = NULL;

	metadata_t m_data;
	uint32_t m_flags = FLAG_MIME|FLAG_DURATION|FLAG_DLNA_PN|FLAG_DATE;
	char *album_art = NULL;

	memset(&m_data, '\0', sizeof(m_data));

	if ( stat(path, &file) != 0 )
		return 0;
	strip_ext(name);
	clean_name = cleanup_name(name);
	year = strip_year(clean_name);

	DPRINTF(L_METADATA, E_DEBUG, "Trying to find online metadata for %s [%s].\n", clean_name, year != NULL ? year : "unknown");

	fetch = curl_easy_init();
	if (!fetch)
		goto curl_error;

	curl_easy_setopt(fetch, CURLOPT_HTTPGET, 1L);

	headers = curl_slist_append(headers, "User-Agent: minidlna");
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, "Accept: application/json");
	curl_easy_setopt(fetch, CURLOPT_HTTPHEADER, headers);

	curl_easy_setopt(fetch, CURLOPT_WRITEFUNCTION, ext_meta_response);

	result = moviedb_search_movies(clean_name, year, fetch, &m_data, &m_flags);
	if (result > 0)
	{
		if (moviedb_get_movie_details(result, fetch, &m_data, &m_flags, &album_art) == 0)
		{
			DPRINTF(L_METADATA, E_DEBUG, "Received movie poster path %s, and genre(s) %s\n",
				album_art != NULL ? album_art : "[none exists]", m_flags & FLAG_GENRE ? m_data.genre : "[unknown]");
		}
	}
	if (result > 0)
	{
		int ret_sql;
		if (detailID)
		{
			ret_sql = sql_exec(db, "UPDATE DETAILS set TITLE = '%q', DATE = %Q, DESCRIPTION = %Q, "
					   "GENRE = %Q, COMMENT = %Q "
					   "WHERE PATH=%Q and ID=%lld;", m_data.title, m_data.date,
					   m_data.description, m_data.genre, m_data.comment, path, detailID);
		}
		else
		{
			ret_sql = sql_exec(db, "INSERT into DETAILS"
					   " (PATH, SIZE, TIMESTAMP, DATE, TITLE, DESCRIPTION, MIME, COMMENT, GENRE) "
					   "VALUES (%Q, %lld, %lld, %Q, '%q', %Q, '%q', %Q, %Q);",
					   path, (long long)file.st_size, (long long)file.st_mtime,
					   m_data.date, m_data.title, m_data.description, m_data.mime,
					   m_data.comment, m_data.genre);
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
	DPRINTF(L_METADATA, E_ERROR, "Failed to process online metadata for %s.\n", clean_name);

cleanup:
	if (headers)
		curl_slist_free_all(headers);
	if (fetch)
		curl_easy_cleanup(fetch);
	if (year)
		free(year);
	if (clean_name)
		free(clean_name);
	free_metadata(&m_data, m_flags);

	return result;
}
