/**
 *  Cell, the Sphere packaging compiler
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

#include "cell.h"
#include "tool.h"

#include "fs.h"
#include "visor.h"

struct tool
{
	unsigned int refcount;
	duk_context* js_ctx;
	void*        callback_ptr;
	char*        verb;
};

tool_t*
tool_new(duk_context* ctx, const char* verb)
{
	void*   callback_ptr;
	tool_t* tool;

	callback_ptr = duk_ref_heapptr(ctx, -1);
	duk_pop(ctx);

	tool = calloc(1, sizeof(tool_t));
	tool->verb = strdup(verb);
	tool->js_ctx = ctx;
	tool->callback_ptr = callback_ptr;
	return tool_ref(tool);
}

tool_t*
tool_ref(tool_t* tool)
{
	if (tool == NULL)
		return NULL;
	++tool->refcount;
	return tool;
}

void
tool_unref(tool_t* tool)
{
	if (tool == NULL || --tool->refcount > 0)
		return;

	duk_unref_heapptr(tool->js_ctx, tool->callback_ptr);
	free(tool->verb);
	free(tool);
}

bool
tool_run(tool_t* tool, visor_t* visor, const fs_t* fs, const path_t* out_path, vector_t* in_paths)
{
	duk_uarridx_t array_index;
	path_t*       dir_path;
	const char*   filename;
	bool          is_outdated = false;
	duk_context*  js_ctx;
	time_t        last_mtime = 0;
	int           line_number;
	int           num_errors;
	bool          result_ok = true;
	struct stat   stats;

	iter_t iter;
	path_t* *p_path;

	if (tool == NULL)
		return true;

	js_ctx = tool->js_ctx;

	visor_begin_op(visor, "%s '%s'", tool->verb, path_cstr(out_path));

	// ensure the target directory exists
	dir_path = path_strip(path_dup(out_path));
	fs_mkdir(fs, path_cstr(dir_path));
	path_free(dir_path);

	if (fs_stat(fs, path_cstr(out_path), &stats) == 0)
		last_mtime = stats.st_mtime;
	duk_push_heapptr(js_ctx, tool->callback_ptr);
	duk_push_string(js_ctx, path_cstr(out_path));
	duk_push_array(js_ctx);
	iter = vector_enum(in_paths);
	while (p_path = iter_next(&iter)) {
		array_index = (duk_uarridx_t)duk_get_length(js_ctx, -1);
		duk_push_string(js_ctx, path_cstr(*p_path));
		duk_put_prop_index(js_ctx, -2, array_index);
	}
	num_errors = visor_num_errors(visor);
	if (duk_pcall(js_ctx, 2) != DUK_EXEC_SUCCESS) {
		duk_get_prop_string(js_ctx, -1, "fileName");
		filename = duk_safe_to_string(js_ctx, -1);
		duk_get_prop_string(js_ctx, -2, "lineNumber");
		line_number = duk_get_int(js_ctx, -1);
		duk_dup(js_ctx, -3);
		duk_to_string(js_ctx, -1);
		visor_error(visor, "%s", duk_get_string(js_ctx, -1));
		visor_print(visor, "@ [%s:%d]", filename, line_number);
		duk_pop_3(js_ctx);
		result_ok = false;
	}
	duk_pop(js_ctx);
	if (visor_num_errors(visor) > num_errors)
		result_ok = false;

	// verify that the tool actually did something.  if the target file doesn't exist,
	// that's definitely an error.  if the target file does exist but the timestamp hasn't changed,
	// only issue a warning because it might have been intentional (unlikely, but possible).
	if (result_ok) {
		if (fs_stat(fs, path_cstr(out_path), &stats) != 0) {
			visor_error(visor, "target file not found after build");
			result_ok = false;
		}
		else if (stats.st_mtime == last_mtime) {
			visor_warn(visor, "target file unchanged after build");
		}
	}
	else {
		// to ensure correctness, delete the target file if there was an error.  if we don't do
		// this, subsequent builds may not work correctly in the case that a tool accidentally
		// writes a target file anyway after producing errors.
		fs_unlink(fs, path_cstr(out_path));
	}

	visor_end_op(visor);
	return result_ok;
}
