/**
 *  miniSphere JavaScript game engine
 *  Copyright (c) 2015-2017, Fat Cerberus
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither the name of miniSphere nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
**/

#include "minisphere.h"
#include "debugger.h"

#include "sockets.h"

struct source
{
	char*      name;
	lstring_t* text;
};

enum appnotify
{
	APPNFY_DEBUG_PRINT = 0x01,
};

enum apprequest
{
	APPREQ_GAME_INFO = 0x01,
	APPREQ_SOURCE = 0x02,
	APPREQ_WATERMARK = 0x03,
};

static const int TCP_DEBUG_PORT = 1208;

static bool       do_attach_debugger   (void);
static void       do_detach_debugger   (bool is_shutdown);
static void       duk_cb_debug_detach  (duk_context* ctx, void* udata);
static duk_idx_t  duk_cb_debug_request (duk_context* ctx, void* udata, duk_idx_t nvalues);
static duk_size_t duk_cb_debug_peek    (void* udata);
static duk_size_t duk_cb_debug_read    (void* udata, char* buffer, duk_size_t bufsize);
static duk_size_t duk_cb_debug_write   (void* udata, const char* data, duk_size_t size);

static bool       s_is_attached = false;
static color_t    s_banner_color;
static lstring_t* s_banner_text;
static bool       s_have_source_map = false;
static server_t*  s_server;
static socket_t*  s_socket;
static vector_t*  s_sources;
static bool       s_want_attach;

void
debugger_init(bool want_attach, bool allow_remote)
{
	void*         data;
	size_t        data_size;
	const path_t* game_root;
	const char*   hostname;

	s_banner_text = lstr_new("debug");
	s_banner_color = color_new(192, 192, 192, 255);
	s_sources = vector_new(sizeof(struct source));

	// load the source map, if one is available
	s_have_source_map = false;
	duk_push_global_stash(g_duk);
	duk_del_prop_string(g_duk, -1, "debugMap");
	game_root = game_path(g_game);
	if (data = game_read_file(g_game, "sources.json", &data_size)) {
		duk_push_lstring(g_duk, data, data_size);
		duk_json_decode(g_duk, -1);
		duk_put_prop_string(g_duk, -2, "debugMap");
		free(data);
		s_have_source_map = true;
	}
	else if (!path_is_file(game_root)) {
		duk_push_object(g_duk);
		duk_push_string(g_duk, path_cstr(game_root));
		duk_put_prop_string(g_duk, -2, "origin");
		duk_put_prop_string(g_duk, -2, "debugMap");
	}
	duk_pop(g_duk);

	// listen for SSj connection on TCP port 1208. the listening socket will remain active
	// for the duration of the session, allowing a debugger to be attached at any time.
	console_log(1, "listening for debugger on TCP port %d", TCP_DEBUG_PORT);
	hostname = allow_remote ? NULL : "127.0.0.1";
	s_server = server_new(hostname, TCP_DEBUG_PORT, 1024, 1);

	// if the engine was started in debug mode, wait for a debugger to connect before
	// beginning execution.
	s_want_attach = want_attach;
	if (s_want_attach && !do_attach_debugger())
		sphere_exit(true);
}

void
debugger_uninit()
{
	iter_t iter;
	struct source* p_source;

	do_detach_debugger(true);
	server_unref(s_server);

	if (s_sources != NULL) {
		iter = vector_enum(s_sources);
		while (p_source = iter_next(&iter)) {
			lstr_free(p_source->text);
			free(p_source->name);
		}
		vector_free(s_sources);
	}
}

void
debugger_update(void)
{
	socket_t* client;

	if (client = server_accept(s_server)) {
		if (s_socket != NULL) {
			console_log(2, "rejected debug connection from %s, already attached",
				socket_hostname(client));
			socket_unref(client);
		}
		else {
			console_log(0, "connected to debug client at %s", socket_hostname(client));
			s_socket = client;
			duk_debugger_detach(g_duk);
			duk_debugger_attach(g_duk,
				duk_cb_debug_read,
				duk_cb_debug_write,
				duk_cb_debug_peek,
				NULL,
				NULL,
				duk_cb_debug_request,
				duk_cb_debug_detach,
				NULL);
			s_is_attached = true;
		}
	}
}

bool
debugger_attached(void)
{
	return s_is_attached;
}

color_t
debugger_color(void)
{
	return s_banner_color;
}

const char*
debugger_name(void)
{
	return lstr_cstr(s_banner_text);
}

const char*
debugger_compiled_name(const char* source_name)
{
	// perform a reverse lookup on the source map to find the compiled name
	// of an asset based on its name in the source tree.  this is needed to
	// support SSj source code download, since SSj only knows the source names.

	static char retval[SPHERE_PATH_MAX];

	const char* this_source;

	strncpy(retval, source_name, SPHERE_PATH_MAX - 1);
	retval[SPHERE_PATH_MAX - 1] = '\0';
	if (!s_have_source_map)
		return retval;
	duk_push_global_stash(g_duk);
	duk_get_prop_string(g_duk, -1, "debugMap");
	if (!duk_get_prop_string(g_duk, -1, "fileMap"))
		duk_pop_3(g_duk);
	else {
		duk_enum(g_duk, -1, DUK_ENUM_OWN_PROPERTIES_ONLY);
		while (duk_next(g_duk, -1, true)) {
			this_source = duk_get_string(g_duk, -1);
			if (strcmp(this_source, source_name) == 0)
				strncpy(retval, duk_get_string(g_duk, -2), SPHERE_PATH_MAX - 1);
			duk_pop_2(g_duk);
		}
		duk_pop_n(g_duk, 4);
	}
	return retval;
}

const char*
debugger_source_name(const char* compiled_name)
{
	// note: pathname must be canonicalized using game_full_path() otherwise
	//       the source map lookup will fail.

	static char retval[SPHERE_PATH_MAX];

	strncpy(retval, compiled_name, SPHERE_PATH_MAX - 1);
	retval[SPHERE_PATH_MAX - 1] = '\0';
	if (!s_have_source_map)
		return retval;
	duk_push_global_stash(g_duk);
	duk_get_prop_string(g_duk, -1, "debugMap");
	if (!duk_get_prop_string(g_duk, -1, "fileMap"))
		duk_pop_3(g_duk);
	else {
		duk_get_prop_string(g_duk, -1, compiled_name);
		if (duk_is_string(g_duk, -1))
			strncpy(retval, duk_get_string(g_duk, -1), SPHERE_PATH_MAX - 1);
		duk_pop_n(g_duk, 4);
	}
	return retval;
}

void
debugger_cache_source(const char* name, const lstring_t* text)
{
	struct source cache_entry;

	iter_t iter;
	struct source* p_source;

	if (s_sources == NULL)
		return;

	iter = vector_enum(s_sources);
	while (p_source = iter_next(&iter)) {
		if (strcmp(name, p_source->name) == 0) {
			lstr_free(p_source->text);
			p_source->text = lstr_dup(text);
			return;
		}
	}

	cache_entry.name = strdup(name);
	cache_entry.text = lstr_dup(text);
	vector_push(s_sources, &cache_entry);
}

void
debugger_log(const char* text, print_op_t op, bool use_console)
{
	const char* heading;

	duk_push_int(g_duk, APPNFY_DEBUG_PRINT);
	duk_push_int(g_duk, (int)op);
	duk_push_string(g_duk, text);
	duk_debugger_notify(g_duk, 3);

	if (use_console) {
		heading = op == PRINT_ASSERT ? "ASSERT"
			: op == PRINT_DEBUG ? "debug"
			: op == PRINT_ERROR ? "ERROR"
			: op == PRINT_INFO ? "info"
			: op == PRINT_TRACE ? "trace"
			: op == PRINT_WARN ? "WARN"
			: "log";
		console_log(0, "%s: %s", heading, text);
	}
}

static bool
do_attach_debugger(void)
{
	double timeout;

	printf("waiting for connection from debug client...\n");
	fflush(stdout);
	timeout = al_get_time() + 30.0;
	while (s_socket == NULL && al_get_time() < timeout) {
		debugger_update();
		sphere_sleep(0.05);
	}
	if (s_socket == NULL)  // did we time out?
		printf("timed out waiting for debug client\n");
	return s_socket != NULL;
}

static void
do_detach_debugger(bool is_shutdown)
{
	if (!s_is_attached)
		return;

	// detach the debugger
	console_log(1, "detaching debug session");
	s_is_attached = false;
	duk_debugger_detach(g_duk);
	if (s_socket != NULL) {
		socket_close(s_socket);
		while (socket_connected(s_socket))
			sphere_sleep(0.05);
	}
	socket_unref(s_socket);
	s_socket = NULL;
	if (s_want_attach && !is_shutdown)
		sphere_exit(true);  // clean detach, exit
}

static void
duk_cb_debug_detach(duk_context* ctx, void* udata)
{
	// note: if s_socket is null, a TCP reset was detected by one of the I/O callbacks.
	// if this is the case, wait a bit for the client to reconnect.
	if (s_socket != NULL || !do_attach_debugger())
		do_detach_debugger(false);
}

static duk_idx_t
duk_cb_debug_request(duk_context* ctx, void* udata, duk_idx_t nvalues)
{
	void*       file_data;
	const char* name;
	int         request_id;
	size2_t     resolution;
	size_t      size;

	iter_t iter;
	struct source* p_source;

	// the first atom must be a request ID number
	if (nvalues < 1 || !duk_is_number(ctx, -nvalues + 0)) {
		duk_push_string(ctx, "missing AppRequest command number");
		return -1;
	}

	request_id = duk_get_int(ctx, -nvalues + 0);
	switch (request_id) {
	case APPREQ_GAME_INFO:
		resolution = game_resolution(g_game);
		duk_push_string(ctx, game_name(g_game));
		duk_push_string(ctx, game_author(g_game));
		duk_push_string(ctx, game_summary(g_game));
		duk_push_int(ctx, resolution.width);
		duk_push_int(ctx, resolution.height);
		return 5;
	case APPREQ_SOURCE:
		if (nvalues < 2) {
			duk_push_string(ctx, "missing filename for Source request");
			return -1;
		}

		name = duk_get_string(ctx, -nvalues + 1);
		name = debugger_compiled_name(name);

		// check if the data is in the source cache
		iter = vector_enum(s_sources);
		while (p_source = iter_next(&iter)) {
			if (strcmp(name, p_source->name) == 0) {
				duk_push_lstring_t(ctx, p_source->text);
				return 1;
			}
		}

		// no cache entry, try loading the file via SphereFS
		if ((file_data = game_read_file(g_game, name, &size))) {
			duk_push_lstring(ctx, file_data, size);
			free(file_data);
			return 1;
		}

		duk_push_sprintf(ctx, "no source available for `%s`", name);
		return -1;
	case APPREQ_WATERMARK:
		if (nvalues < 2 || !duk_is_string(ctx, -nvalues + 1)) {
			duk_push_string(ctx, "missing or invalid debugger name string");
			return -1;
		}

		s_banner_text = duk_require_lstring_t(ctx, -nvalues + 1);
		if (nvalues >= 4) {
			s_banner_color = color_new(
				duk_get_int(ctx, -nvalues + 2),
				duk_get_int(ctx, -nvalues + 3),
				duk_get_int(ctx, -nvalues + 4),
				255);
		}
		return 0;
	default:
		duk_push_sprintf(ctx, "invalid AppRequest command number `%d`", request_id);
		return -1;
	}
}

static duk_size_t
duk_cb_debug_peek(void* udata)
{
	// if the JavaScript interpreter gets stuck in an infinite loop, the engine
	// will be locked out of the event loop and SSj won't be able to communicate
	// with us.  this works around the issue.
	sphere_run(false);

	return socket_peek(s_socket);
}

static duk_size_t
duk_cb_debug_read(void* udata, char* buffer, duk_size_t bufsize)
{
	size_t n_bytes;

	if (s_socket == NULL)
		return 0;

	// if we return zero, Duktape will drop the session. thus we're forced
	// to block until we can read >= 1 byte.
	while (!(n_bytes = socket_peek(s_socket))) {
		if (!socket_connected(s_socket)) {  // did a pig eat it?
			console_log(1, "TCP connection reset while debugging");
			socket_unref(s_socket);
			s_socket = NULL;
			return 0;  // stupid pig
		}

		// so the system doesn't think we locked up...
		sphere_sleep(0.05);
	}

	// let's not overflow the buffer, alright?
	if (n_bytes > bufsize)
		n_bytes = bufsize;
	socket_read(s_socket, buffer, n_bytes);
	return n_bytes;
}

static duk_size_t
duk_cb_debug_write(void* udata, const char* data, duk_size_t size)
{
	if (s_socket == NULL)
		return 0;

	// make sure we're still connected
	if (!socket_connected(s_socket)) {
		console_log(1, "TCP connection reset while debugging");
		socket_unref(s_socket);
		s_socket = NULL;
		return 0;  // stupid pig!
	}

	// send out the data
	socket_write(s_socket, data, size);
	return size;
}
