/*
 * Copyright (C) 2008 Greg Kroah-Hartman <greg@kroah.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <curl/curl.h>
#include "list.h"
#include "smugbatch_version.h"


#define dbg(format, arg...)						\
	do {								\
		if (debug)						\
			printf("%s: " format , __func__ , ## arg );	\
	} while (0)


static char *api_key = "ABW1oenNznek2rD4AIiFn7OhkEkmzEIb";
static char *user_agent = "smugbatch/"SMUGBATCH_VERSION" (greg@kroah.com)";

static char *password;
static char *email;
static char *session_id;
static int debug;

static char *session_id_tag = "Session id";

static char *smugmug_album_list_url = "https://api.smugmug.com/hack/rest/1.2.0/?method=smugmug.albums.get&SessionID=%s&APIKey=%s";
static char *smugmug_login_url = "https://api.smugmug.com/hack/rest/1.2.0/?method=smugmug.login.withPassword&EmailAddress=%s&Password=%s&APIKey=%s";
static char *smugmug_logout_url = "https://api.smugmug.com/hack/rest/1.2.0/?method=smugmug.logout&SessionID=%s&APIKey=%s";

struct album {
	struct list_head entry;
	char *id;
	char *key;
	char *title;
};

static LIST_HEAD(album_list);

static char *find_value(const char *haystack, const char *needle,
			char **new_pos)
{
	char *location;
	char *temp;
	char *value;

	location = strstr(haystack, needle);
	if (!location)
		return NULL;

	value = malloc(1000);
	if (!value)
		return NULL;

	location += strlen(needle);
	temp = value;
	++location;	/* '=' */
	++location;	/* '"' */
	while (*location != '"') {
		*temp = *location;
		++temp;
		++location;
	}
	*temp = '\0';
	if (new_pos)
		*new_pos = location;
	return value;
}

static int sanitize_buffer(char *buffer, size_t size, size_t nmemb)
{
	size_t buffer_size = size * nmemb;
	char *temp;

	if ((!buffer) || (!buffer_size))
		return -EINVAL;

	/* we aren't supposed to get a \0 terminated string, so make sure */
	temp = buffer;
	temp[buffer_size-1] = '\0';
	return 0;
}

static size_t parse_login(void *buffer, size_t size, size_t nmemb, void *userp)
{
	size_t buffer_size = size * nmemb;
	char *temp = buffer;

	session_id = NULL;

	if (sanitize_buffer(buffer, size, nmemb))
		goto exit;

	dbg("buffer = '%s'\n", temp);
	session_id = find_value(buffer, session_id_tag, NULL);

	dbg("session_id = %s\n", session_id);

exit:
	if (!session_id)
		dbg("SessionID not found!");
	return buffer_size;
}

static size_t parse_albums(void *buffer, size_t size, size_t nmemb, void *userp)
{
	size_t buffer_size = size * nmemb;
	char *temp = buffer;
	struct album *album;
	char *id;
	char *key;
	char *title;

	if (sanitize_buffer(buffer, size, nmemb))
		goto exit;

	dbg("%s: buffer = '%s'\n", __func__, temp);

	while (1) {
		id = find_value(temp, "Album id", &temp);
		if (!id)
			break;
		key = find_value(temp, "Key", &temp);
		if (!key)
			break;
		title = find_value(temp, "Title", &temp);
		if (!title)
			break;
		dbg("%s: %s: %s\n", id, key, title);
		album = malloc(sizeof(*album));
		album->id = id;
		album->key = key;
		album->title = title;
		list_add_tail(&album->entry, &album_list);
	}

exit:
	return buffer_size;
}


static size_t parse_logout(void *buffer, size_t size, size_t nmemb, void *userp)
{
	size_t buffer_size = size * nmemb;
	char *temp = buffer;

	if (sanitize_buffer(buffer, size, nmemb))
		goto exit;

	dbg("%s: buffer = '%s'\n", __func__, temp);

exit:
	return buffer_size;
}


static void display_help(void)
{
	printf("help goes here...\n");
}

int main(int argc, char *argv[], char *envp[])
{
	CURL *curl = NULL;
	CURLcode res;
	static char url[1000];
	int option;
	char *filename;
	static const struct option options[] = {
		{ "debug", 0, NULL, 'd' },
		{ "email", 1, NULL, 'e' },
		{ "password", 1, NULL, 'p' },
		{ "help", 0, NULL, 'h' },
		{ }
	};

	while (1) {
		option = getopt_long(argc, argv, "de:p:h", options, NULL);
		if (option == -1)
			break;
		switch (option) {
		case 'd':
			debug = 1;
			break;
		case 'e':
			email = strdup(optarg);
			dbg("email = %s\n", email);
			break;
		case 'p':
			password = strdup(optarg);
			dbg("password = %s\n", password);
			break;
		case 'h':
			display_help();
			goto exit;
		default:
			display_help();
			goto exit;
		}
	}
	filename = argv[optind];

	if ((!email) || (!password)) {
		display_help();
		goto exit;
	}

	sprintf(url, smugmug_login_url, email, password, api_key);
	dbg("url = %s\n", url);

	curl = curl_easy_init();
	if (!curl) {
		printf("Can not init CURL!\n");
		return 1;
	}

	curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);

	curl_easy_setopt(curl, CURLOPT_URL, url);

	//#ifdef SKIP_PEER_VERIFICATION
	/*
	* If you want to connect to a site who isn't using a certificate that is
	* signed by one of the certs in the CA bundle you have, you can skip the
	* verification of the server's certificate. This makes the connection
	* A LOT LESS SECURE.
	*
	* If you have a CA cert for the server stored someplace else than in the
	* default bundle, then the CURLOPT_CAPATH option might come handy for
	* you.
	*/
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
	//#endif

	//#ifdef SKIP_HOSTNAME_VERFICATION
	/*
	* If the site you're connecting to uses a different host name that what
	* they have mentioned in their server certificate's commonName (or
	* subjectAltName) fields, libcurl will refuse to connect. You can skip
	* this check, but this will make the connection less secure.
	*/
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
	//#endif

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, parse_login);
	res = curl_easy_perform(curl);
	if (res) {
		printf("error(%d) trying to login\n", res);
		goto exit;
	}

	if (!session_id) {
		printf("session_id was not found, exiting\n");
		goto exit;
	}

	/* Get list of albums for this user */
	sprintf(url, smugmug_album_list_url, session_id, api_key);
	dbg("url = %s\n", url);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, parse_albums);
	res = curl_easy_perform(curl);
	if (res) {
		printf("error(%d) trying to read list of albums\n", res);
		goto exit;
	}

	{
		struct album *album;
		printf("Availble albums:\nAlbum ID\tAlbum Name\n");
		list_for_each_entry(album, &album_list, entry) {
			printf("%s\t\t%s\n", album->id, album->title);
			}
		printf("\nWhich Album ID to upload to?");

	}

	/* logout */
	sprintf(url, smugmug_logout_url, session_id, api_key);
	dbg("url = %s\n", url);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, parse_logout);
	res = curl_easy_perform(curl);
	if (res) {
		printf("error(%d) trying to logout\n", res);
		goto exit;
	}


exit:
	if (curl)
		curl_easy_cleanup(curl);
	if (email)
		free(email);
	if (password)
		free(password);
	if (session_id)
		free(session_id);

	return 0;
}
