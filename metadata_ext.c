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
#include "metadata_ext.h"
#include "metadata.h"
#include "utils.h"
#include "sql.h"

#define CURL_FETCH_BUFFER_SZ        	4096
#define CURL_MOVIEDB_SEARCH_MOVIES	"https://api.themoviedb.org/3/search/movie?api_key=%s&query=%s"
#define CURL_MOVIEDB_SEARCH_TVSHOW	"https://api.themoviedb.org/3/search/tv?api_key=%s&query=%s"
#define CURL_MOVIEDB_QUERY_WITH_YEAR    "%s&year=%s"
#define CURL_MOVIEDB_GENRES_MOVIES      "https://api.themoviedb.org/3/genre/movie/list?api_key=%s"
#define CURL_MOVIEDB_GENRES_TVSHOWS     "https://api.themoviedb.org/3/genre/tv/list?api_key=%s"

struct write_result
{
	char *data;
	int pos;
};

static bool curl_is_initialized = false;
static const char* themoviedb_api_key = "3bf0eed11dcc07615f983d6dbc410194";

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

	DPRINTF(L_HTTP, E_INFO, "Initializing external metadata facility...\n");

	curl_ver = curl_version_info(CURLVERSION_NOW);
	DPRINTF(L_HTTP, E_DEBUG, "Using libCURL version %s.\n", curl_ver->version);
	curl_global_init(CURL_GLOBAL_ALL);
	curl_is_initialized = true;
}

static size_t ext_meta_response(void *ptr, size_t size, size_t nmemb, void *stream)
{
	struct write_result *result = (struct write_result *)stream;

	if(result->pos + size * nmemb >= CURL_FETCH_BUFFER_SZ - 1)
	{
		DPRINTF(L_HTTP, E_ERROR, "External metadata response buffer is too small (required %ld)\n", result->pos + size * nmemb);
		return 0;
	}

	memcpy(result->data + result->pos, ptr, size * nmemb);
	result->pos += size * nmemb;

	return size * nmemb;
}

int64_t
search_ext_meta(const char *path, char *name, int64_t detailID)
{
	int64_t result = 0;
	CURL *fetch;
	CURLcode fetch_status;
	long fetch_code;
	struct curl_slist *headers = NULL;
	char *data = NULL;
	char query[MAXPATHLEN];
	char *escaped = NULL;

	struct stat file;
	char *clean_name, *year;

	json_t *db_root;
	json_error_t db_err;
	metadata_t m;

	memset(&m, '\0', sizeof(m));
	memset(query, 0, sizeof(query));

	if ( stat(path, &file) != 0 )
		return 0;
	strip_ext(name);
	clean_name = cleanup_name(name);
	year = strip_year(clean_name);

	DPRINTF(L_METADATA, E_DEBUG, "Trying to find online metadata for %s [%s].\n", clean_name, year != NULL ? year : "unknown");

	fetch = curl_easy_init();
	if (!fetch)
		goto curl_error;

	data = malloc(CURL_FETCH_BUFFER_SZ);
	if (!data)
		goto curl_error;

	struct write_result write_result = {
		.data = data,
		.pos = 0
	};

	/* compile query */
	escaped = curl_easy_escape(fetch, clean_name, 0);
	snprintf(query, MAXPATHLEN, CURL_MOVIEDB_SEARCH_MOVIES, themoviedb_api_key, escaped);
	curl_free(escaped);
	if (year)
	{
		escaped = strdup(query);
		snprintf(query, MAXPATHLEN, CURL_MOVIEDB_QUERY_WITH_YEAR, escaped, year);
		free(escaped);
	}

	DPRINTF(L_HTTP, E_DEBUG, "External metadata URL: %s\n", query);
	curl_easy_setopt(fetch, CURLOPT_URL, query);

	curl_easy_setopt(fetch, CURLOPT_HTTPGET, 1L);

	headers = curl_slist_append(headers, "User-Agent: minidlna");
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, "Accept: application/json");
	curl_easy_setopt(fetch, CURLOPT_HTTPHEADER, headers);

	curl_easy_setopt(fetch, CURLOPT_WRITEFUNCTION, ext_meta_response);
	curl_easy_setopt(fetch, CURLOPT_WRITEDATA, &write_result);

	fetch_status = curl_easy_perform(fetch);
	if (fetch_status != 0)
	{
		DPRINTF(L_HTTP, E_ERROR, "External metadata fetch error: %s\n", curl_easy_strerror(fetch_status));
		goto curl_error;
	}
	curl_easy_getinfo(fetch, CURLINFO_RESPONSE_CODE, &fetch_code);
	if (fetch_code != 200)
	{
		DPRINTF(L_HTTP, E_WARN, "External metadata server response: %ld\n", fetch_code);
	}
	data[write_result.pos] = '\0';
	DPRINTF(L_METADATA, E_DEBUG, "External metadata response: %s\n", data);

	db_root = json_loads(data, 0, &db_err);
	if (!db_root)
	{
		DPRINTF(L_METADATA, E_ERROR, "Failed to parse external metadata response: [%d] %s\n",
			db_err.line, db_err.text);
		goto curl_error;
	}
	if (!json_is_object(db_root))
	{
		DPRINTF(L_METADATA, E_ERROR, "Failed to parse external metadata response: not an object.\n");
		json_decref(db_root);
		goto curl_error;
	}
	json_t *object = json_object_get(db_root, "total_results");
	if (!json_is_integer(object))
	{
		DPRINTF(L_METADATA, E_ERROR, "Failed to parse external metadata response: not an object.\n");
		json_decref(db_root);
		goto curl_error;
	}
	json_int_t db_total_results = json_integer_value(object);
	if (db_total_results > 0)
	{
		object = json_object_get(db_root, "results");
		if (!json_is_array(object))
		{
			DPRINTF(L_METADATA, E_ERROR, "Failed to parse external metadata response: not an object.\n");
			json_decref(db_root);
			goto curl_error;
		}
		json_t *record = json_array_get(object, 0);
		object = json_object_get(record, "title");
		if (json_is_string(object))
			m.title = strdup(json_string_value(object));
		object = json_object_get(record, "release_date");
		if (json_is_string(object))
			m.date = strdup(json_string_value(object));
		object = json_object_get(record, "overview");
		if (json_is_string(object))
			m.comment = strdup(json_string_value(object));

		int ret_sql;
		if (detailID)
		{
			ret_sql = sql_exec(db, "UPDATE DETAILS set TITLE = '%q', DATE = %Q, COMMENT = %Q "
					   "WHERE PATH=%Q and ID=%lld;", m.title, m.date, m.comment,
					   path, detailID);
		}
		else
		{
			ret_sql = sql_exec(db, "INSERT into DETAILS"
					   " (PATH, SIZE, TIMESTAMP, DATE, TITLE, COMMENT, MIME) "
					   "VALUES (%Q, %lld, %lld, %Q, '%q', %Q, '%q');",
					   path, (long long)file.st_size, (long long)file.st_mtime,
					   m.date, m.title, m.comment, m.mime);
		}
		if( ret_sql != SQLITE_OK )
		{
			DPRINTF(E_ERROR, L_METADATA, "Error inserting/updating details for '%s'!\n", path);
			ret_sql = 0;
		}
		else
		{
			ret_sql = sqlite3_last_insert_rowid(db);
		}
		free_metadata(&m, 0xFFFFFFFF);
		json_decref(db_root);
		result = ret_sql;
	}

	goto cleanup;

curl_error:
	DPRINTF(L_METADATA, E_ERROR, "Failed to process online metadata for %s.\n", clean_name);

cleanup:
	if (headers)
		curl_slist_free_all(headers);
	if (data)
		free(data);
	if (fetch)
		curl_easy_cleanup(fetch);
	free(clean_name);
	if (year)
		free(year);

	return result;
}
