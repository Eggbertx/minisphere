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
#include "build.h"

#include "api.h"
#include "fs.h"
#include "spk_writer.h"
#include "target.h"
#include "tool.h"
#include "utility.h"
#include "visor.h"
#include "xoroshiro.h"

struct build
{
	vector_t*     artifacts;
	fs_t*         fs;
	duk_context*  js_context;
	vector_t*     targets;
	time_t        timestamp;
	visor_t*      visor;
};

enum file_op
{
	FILE_OP_READ,
	FILE_OP_WRITE,
	FILE_OP_UPDATE,
	FILE_OP_MAX,
};

static duk_ret_t js_require                       (duk_context* ctx);
static duk_ret_t js_error                         (duk_context* ctx);
static duk_ret_t js_files                         (duk_context* ctx);
static duk_ret_t js_install                       (duk_context* ctx);
static duk_ret_t js_warn                          (duk_context* ctx);
static duk_ret_t js_Sphere_get_Game               (duk_context* ctx);
static duk_ret_t js_Sphere_get_Platform           (duk_context* ctx);
static duk_ret_t js_Sphere_get_Version            (duk_context* ctx);
static duk_ret_t js_new_DirectoryStream           (duk_context* ctx);
static duk_ret_t js_DirectoryStream_finalize      (duk_context* ctx);
static duk_ret_t js_DirectoryStream_get_fileCount (duk_context* ctx);
static duk_ret_t js_DirectoryStream_get_fileName  (duk_context* ctx);
static duk_ret_t js_DirectoryStream_get_position  (duk_context* ctx);
static duk_ret_t js_DirectoryStream_set_position  (duk_context* ctx);
static duk_ret_t js_DirectoryStream_next          (duk_context* ctx);
static duk_ret_t js_DirectoryStream_rewind        (duk_context* ctx);
static duk_ret_t js_FS_createDirectory            (duk_context* ctx);
static duk_ret_t js_FS_deleteFile                 (duk_context* ctx);
static duk_ret_t js_FS_directoryExists            (duk_context* ctx);
static duk_ret_t js_FS_fileExists                 (duk_context* ctx);
static duk_ret_t js_FS_fullPath                   (duk_context* ctx);
static duk_ret_t js_FS_readFile                   (duk_context* ctx);
static duk_ret_t js_FS_relativePath               (duk_context* ctx);
static duk_ret_t js_FS_rename                     (duk_context* ctx);
static duk_ret_t js_FS_removeDirectory            (duk_context* ctx);
static duk_ret_t js_FS_writeFile                  (duk_context* ctx);
static duk_ret_t js_new_FileStream                (duk_context* ctx);
static duk_ret_t js_FileStream_finalize           (duk_context* ctx);
static duk_ret_t js_FileStream_dispose            (duk_context* ctx);
static duk_ret_t js_FileStream_get_fileSize       (duk_context* ctx);
static duk_ret_t js_FileStream_get_position       (duk_context* ctx);
static duk_ret_t js_FileStream_set_position       (duk_context* ctx);
static duk_ret_t js_FileStream_read               (duk_context* ctx);
static duk_ret_t js_FileStream_write              (duk_context* ctx);
static duk_ret_t js_RNG_fromSeed                  (duk_context* ctx);
static duk_ret_t js_RNG_fromState                 (duk_context* ctx);
static duk_ret_t js_new_RNG                       (duk_context* ctx);
static duk_ret_t js_RNG_finalize                  (duk_context* ctx);
static duk_ret_t js_RNG_get_state                 (duk_context* ctx);
static duk_ret_t js_RNG_set_state                 (duk_context* ctx);
static duk_ret_t js_RNG_next                      (duk_context* ctx);
static duk_ret_t js_new_Tool                      (duk_context* ctx);
static duk_ret_t js_Tool_finalize                 (duk_context* ctx);
static duk_ret_t js_Tool_stage                    (duk_context* ctx);
static duk_ret_t js_Target_finalize               (duk_context* ctx);
static duk_ret_t js_Target_get_fileName           (duk_context* ctx);
static duk_ret_t js_Target_get_name               (duk_context* ctx);

static void       clean_old_artifacts  (build_t* build, bool keep_targets);
static duk_bool_t eval_cjs_module      (duk_context* ctx, fs_t* fs, const char* filename, bool as_mjs);
static path_t*    find_cjs_module      (duk_context* ctx, fs_t* fs, const char* id, const char* origin, const char* sys_origin);
static duk_ret_t  install_target       (duk_context* ctx);
static path_t*    load_package_json    (duk_context* ctx, const char* filename);
static void       make_file_targets    (fs_t* fs, const char* wildcard, const path_t* path, const path_t* subdir, vector_t* targets, bool recursive, time_t timestamp);
static void       push_require         (duk_context* ctx, const char* module_id);
static int        sort_targets_by_path (const void* p_a, const void* p_b);
static bool       write_manifests      (build_t* build);

build_t*
build_new(const path_t* source_path, const path_t* out_path)
{
	vector_t*    artifacts;
	build_t*     build;
	duk_context* ctx;
	char*        filename;
	fs_t*        fs;
	char*        json;
	size_t       json_size;
	visor_t*     visor;

	build = calloc(1, sizeof(build_t));
	visor = visor_new();
	fs = fs_new(path_cstr(source_path), path_cstr(out_path), NULL);
	ctx = duk_create_heap(NULL, NULL, NULL, build, NULL);

	visor_begin_op(visor, "setting up Cellscript environment");

	// initialize the CommonJS cache and global require()
	duk_push_global_stash(ctx);
	duk_push_bare_object(ctx);
	duk_put_prop_string(ctx, -2, "moduleCache");
	duk_pop(ctx);

	duk_push_global_object(ctx);
	duk_push_string(ctx, "require");
	push_require(ctx, NULL);
	duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE
		| DUK_DEFPROP_CLEAR_ENUMERABLE
		| DUK_DEFPROP_SET_WRITABLE
		| DUK_DEFPROP_SET_CONFIGURABLE);

	// prepare environment for ECMAScript 2015 support
	if (fs_fexist(fs, "#/shim.js") && !eval_cjs_module(ctx, fs, "#/shim.js", false))
		return false;
	if (fs_fexist(fs, "#/typescript.js")) {
		duk_push_global_stash(ctx);
		if (eval_cjs_module(ctx, fs, "#/typescript.js", false))
			duk_put_prop_string(ctx, -2, "TypeScript");
		else
			duk_pop(ctx);
		duk_pop(ctx);
	}

	// initialize the Cellscript API
	api_init(ctx);
	api_define_function(ctx, NULL, "error", js_error);
	api_define_function(ctx, NULL, "files", js_files);
	api_define_function(ctx, NULL, "install", js_install);
	api_define_function(ctx, NULL, "warn", js_warn);
	api_define_static_prop(ctx, "Sphere", "Game", js_Sphere_get_Game, NULL);
	api_define_static_prop(ctx, "Sphere", "Platform", js_Sphere_get_Platform, NULL);
	api_define_static_prop(ctx, "Sphere", "Version", js_Sphere_get_Version, NULL);
	api_define_class(ctx, "DirectoryStream", js_new_DirectoryStream, js_DirectoryStream_finalize);
	api_define_property(ctx, "DirectoryStream", "fileCount", js_DirectoryStream_get_fileCount, NULL);
	api_define_property(ctx, "DirectoryStream", "fileName", js_DirectoryStream_get_fileName, NULL);
	api_define_property(ctx, "DirectoryStream", "position", js_DirectoryStream_get_position, js_DirectoryStream_set_position);
	api_define_method(ctx, "DirectoryStream", "next", js_DirectoryStream_next);
	api_define_method(ctx, "DirectoryStream", "rewind", js_DirectoryStream_rewind);
	api_define_function(ctx, "FS", "createDirectory", js_FS_createDirectory);
	api_define_function(ctx, "FS", "deleteFile", js_FS_deleteFile);
	api_define_function(ctx, "FS", "directoryExists", js_FS_directoryExists);
	api_define_function(ctx, "FS", "fileExists", js_FS_fileExists);
	api_define_function(ctx, "FS", "fullPath", js_FS_fullPath);
	api_define_function(ctx, "FS", "readFile", js_FS_readFile);
	api_define_function(ctx, "FS", "relativePath", js_FS_relativePath);
	api_define_function(ctx, "FS", "removeDirectory", js_FS_removeDirectory);
	api_define_function(ctx, "FS", "rename", js_FS_rename);
	api_define_function(ctx, "FS", "writeFile", js_FS_writeFile);
	api_define_class(ctx, "FileStream", js_new_FileStream, js_FileStream_finalize);
	api_define_property(ctx, "FileStream", "fileSize", js_FileStream_get_fileSize, NULL);
	api_define_property(ctx, "FileStream", "position", js_FileStream_get_position, js_FileStream_set_position);
	api_define_method(ctx, "FileStream", "dispose", js_FileStream_dispose);
	api_define_method(ctx, "FileStream", "read", js_FileStream_read);
	api_define_method(ctx, "FileStream", "write", js_FileStream_write);
	api_define_class(ctx, "RNG", js_new_RNG, js_RNG_finalize);
	api_define_function(ctx, "RNG", "fromSeed", js_RNG_fromSeed);
	api_define_function(ctx, "RNG", "fromState", js_RNG_fromState);
	api_define_property(ctx, "RNG", "state", js_RNG_get_state, js_RNG_set_state);
	api_define_method(ctx, "RNG", "next", js_RNG_next);
	api_define_class(ctx, "Target", NULL, js_Target_finalize);
	api_define_property(ctx, "Target", "fileName", js_Target_get_fileName, NULL);
	api_define_property(ctx, "Target", "name", js_Target_get_name, NULL);
	api_define_class(ctx, "Tool", js_new_Tool, js_Tool_finalize);
	api_define_method(ctx, "Tool", "stage", js_Tool_stage);

	api_define_const(ctx, "FileOp", "Read", FILE_OP_READ);
	api_define_const(ctx, "FileOp", "Write", FILE_OP_WRITE);
	api_define_const(ctx, "FileOp", "Update", FILE_OP_UPDATE);

	// game manifest (gets JSON encoded at end of build)
	duk_push_global_stash(ctx);
	duk_push_object(ctx);
	duk_put_prop_string(ctx, -2, "descriptor");
	duk_pop(ctx);

	// create a Tool for the install() function to use
	duk_push_global_stash(ctx);
	duk_push_c_function(ctx, install_target, DUK_VARARGS);
	duk_push_class_obj(ctx, "Tool", tool_new(ctx, "installing"));
	duk_put_prop_string(ctx, -2, "installTool");
	duk_pop(ctx);

	// load artifacts from previous build
	artifacts = vector_new(sizeof(char*));
	if (json = fs_fslurp(fs, "@/artifacts.json", &json_size)) {
		duk_push_lstring(ctx, json, json_size);
		free(json);
		if (duk_json_pdecode(ctx) == DUK_EXEC_SUCCESS && duk_is_array(ctx, -1)) {
			duk_enum(ctx, -1, DUK_ENUM_ARRAY_INDICES_ONLY);
			while (duk_next(ctx, -1, true)) {
				filename = strdup(duk_to_string(ctx, -1));
				vector_push(artifacts, &filename);
				duk_pop_2(ctx);
			}
		}
		duk_pop(ctx);
	}

	visor_end_op(visor);

	build->visor = visor;
	build->fs = fs;
	build->js_context = ctx;
	build->artifacts = artifacts;
	build->targets = vector_new(sizeof(target_t*));
	return build;
}

void
build_free(build_t* build)
{
	iter_t iter;

	if (build == NULL)
		return;

	printf("%d error(s), %d warning(s).\n",
		visor_num_errors(build->visor),
		visor_num_warns(build->visor));

	iter = vector_enum(build->artifacts);
	while (iter_next(&iter))
		free(*(char**)iter.ptr);
	iter = vector_enum(build->targets);
	while (iter_next(&iter))
		target_free(*(target_t**)iter.ptr);

	duk_destroy_heap(build->js_context);
	fs_free(build->fs);
	visor_free(build->visor);
	free(build);
}

bool
build_eval(build_t* build, const char* filename)
{
	const char* err_filename;
	int         err_line;
	bool        is_mjs;
	bool        is_ok = true;
	path_t*     path;
	struct stat stats;

	if (fs_stat(build->fs, filename, &stats) != 0)
		return false;

	visor_begin_op(build->visor, "evaluating '%s'", filename);
	build->timestamp = stats.st_mtime;
	path = path_new(filename);
	is_mjs = path_has_extension(path, ".mjs");
	path_free(path);
	if (!eval_cjs_module(build->js_context, build->fs, filename, is_mjs)) {
		is_ok = false;
		duk_get_prop_string(build->js_context, -1, "fileName");
		err_filename = duk_safe_to_string(build->js_context, -1);
		duk_get_prop_string(build->js_context, -2, "lineNumber");
		err_line = duk_get_int(build->js_context, -1);
		duk_dup(build->js_context, -3);
		duk_to_string(build->js_context, -1);
		visor_error(build->visor, "%s", duk_get_string(build->js_context, -1));
		visor_print(build->visor, "@ [%s:%d]", err_filename, err_line);
		duk_pop_3(build->js_context);
	}
	duk_pop(build->js_context);
	visor_end_op(build->visor);
	return is_ok;
}

bool
build_clean(build_t* build)
{
	clean_old_artifacts(build, false);
	fs_unlink(build->fs, "@/artifacts.json");
	return true;
}

bool
build_package(build_t* build, const char* filename)
{
	const path_t* in_path;
	path_t*       out_path;
	spk_writer_t* spk;

	iter_t iter;
	target_t** p_target;

	visor_begin_op(build->visor, "packaging game to '%s'", filename);
	spk = spk_create(filename);
	spk_add_file(spk, build->fs, "@/game.json", "game.json");
	spk_add_file(spk, build->fs, "@/game.sgm", "game.sgm");
	spk_add_file(spk, build->fs, "@/sources.json", "sources.json");
	iter = vector_enum(build->targets);
	while (p_target = iter_next(&iter)) {
		in_path = target_path(*p_target);
		if (path_num_hops(in_path) == 0 || !path_hop_is(in_path, 0, "@"))
			continue;
		out_path = path_dup(target_path(*p_target));
		path_remove_hop(out_path, 0);
		visor_begin_op(build->visor, "packaging '%s'", path_cstr(out_path));
		spk_add_file(spk, build->fs,
			path_cstr(target_path(*p_target)),
			path_cstr(out_path));
		path_free(out_path);
		visor_end_op(build->visor);
	}
	spk_close(spk);
	visor_end_op(build->visor);
	return true;
}

bool
build_run(build_t* build, bool want_debug, bool rebuild_all)
{
	const char*   filename;
	vector_t*     filenames;
	const char*   json;
	size_t        json_size;
	const char*   last_filename = "";
	int           num_matches = 1;
	const path_t* path;
	const path_t* source_path;
	vector_t*     sorted_targets;

	iter_t iter;
	target_t** p_target;

	// ensure there are no conflicting targets before building.  to simplify the check,
	// we sort the targets by filename first and then look for runs of identical filenames.
	// by doing this, we only have to walk the list once.
	visor_begin_op(build->visor, "building targets", vector_len(build->targets));
	sorted_targets = vector_dup(build->targets);
	vector_sort(sorted_targets, sort_targets_by_path);
	iter = vector_enum(sorted_targets);
	while (iter_next(&iter)) {
		filename = path_cstr(target_path(*(target_t**)iter.ptr));
		if (strcmp(filename, last_filename) == 0)
			++num_matches;
		else {
			if (num_matches > 1)
				visor_error(build->visor, "%d-way conflict '%s'", num_matches, filename);
			num_matches = 1;
		}
		last_filename = filename;
	}
	if (num_matches > 1)
		visor_error(build->visor, "%d-way conflict '%s'", num_matches, filename);
	vector_free(sorted_targets);
	if (visor_num_errors(build->visor) > 0) {
		visor_end_op(build->visor);
		goto finished;
	}

	// build all relevant targets
	iter = vector_enum(build->targets);
	while (p_target = iter_next(&iter)) {
		path = target_path(*p_target);
		if (path_num_hops(path) == 0 || !path_hop_is(path, 0, "@"))
			continue;
		target_build(*p_target, build->visor, rebuild_all);
	}
	visor_end_op(build->visor);

	// only generate a game manifest if the build finished with no errors.
	// warnings are fine.
	if (visor_num_errors(build->visor) == 0) {
		clean_old_artifacts(build, true);
		if (!write_manifests(build)) {
			fs_unlink(build->fs, "@/game.json");
			fs_unlink(build->fs, "@/game.sgm");
			goto finished;
		}
	}
	else {
		// delete any existing game manifest to ensure we don't accidentally
		// generate a functional but broken distribution.
		fs_unlink(build->fs, "@/game.json");
		fs_unlink(build->fs, "@/game.sgm");
		goto finished;
	}

	// generate the source map
	if (want_debug) {
		visor_begin_op(build->visor, "writing source map");
		duk_push_object(build->js_context);
		duk_push_object(build->js_context);
		iter = vector_enum(build->targets);
		while (p_target = iter_next(&iter)) {
			path = target_path(*p_target);
			if (path_num_hops(path) == 0 || !path_hop_is(path, 0, "@"))
				continue;
			if (!(source_path = target_source_path(*p_target)))
				continue;
			duk_push_string(build->js_context, path_cstr(path));
			duk_push_string(build->js_context, path_cstr(source_path));
			duk_put_prop(build->js_context, -3);
		}
		duk_put_prop_string(build->js_context, -2, "fileMap");
		duk_json_encode(build->js_context, -1);
		json = duk_get_lstring(build->js_context, -1, &json_size);
		fs_fspew(build->fs, "@/sources.json", json, json_size);
		duk_pop(build->js_context);
		visor_end_op(build->visor);
	}
	else {
		fs_unlink(build->fs, "@/sources.json");
	}

	filenames = visor_filenames(build->visor);
	duk_push_array(build->js_context);
	iter = vector_enum(filenames);
	while (iter_next(&iter)) {
		duk_push_string(build->js_context, *(char**)iter.ptr);
		duk_put_prop_index(build->js_context, -2, (duk_uarridx_t)iter.index);
	}
	duk_json_encode(build->js_context, -1);
	json = duk_get_lstring(build->js_context, -1, &json_size);
	fs_fspew(build->fs, "@/artifacts.json", json, json_size);

finished:
	return visor_num_errors(build->visor) == 0;
}

static void
clean_old_artifacts(build_t* build, bool keep_targets)
{
	vector_t* filenames;
	bool      keep_file;

	iter_t iter_i, iter_j;

	visor_begin_op(build->visor, "cleaning up old build artifacts");
	filenames = visor_filenames(build->visor);
	iter_i = vector_enum(build->artifacts);
	while (iter_next(&iter_i)) {
		keep_file = false;
		if (keep_targets) {
			iter_j = vector_enum(filenames);
			while (iter_next(&iter_j)) {
				if (strcmp(*(char**)iter_j.ptr, *(char**)iter_i.ptr) == 0)
					keep_file = true;
			}
		}
		if (!keep_file) {
			visor_begin_op(build->visor, "removing '%s'", *(char**)iter_i.ptr);
			fs_unlink(build->fs, *(char**)iter_i.ptr);
			visor_end_op(build->visor);
		}
	}
	visor_end_op(build->visor);
}

static duk_bool_t
eval_cjs_module(duk_context* ctx, fs_t* fs, const char* filename, bool as_mjs)
{
	// HERE BE DRAGONS!
	// this function is horrendous.  Duktape's stack-based API is powerful, but gets
	// very messy very quickly when dealing with object properties.  I tried to add
	// comments to illuminate what's going on, but it's still likely to be confusing for
	// someone not familiar with Duktape code.  proceed with caution.

	// notes:
	//     - the final value of `module.exports` is left on top of the Duktape value stack.
	//     - `module.id` is set to the given filename.  in order to guarantee proper cache
	//       behavior, the filename should be in canonical form.
	//     - this is a protected call.  if the module being loaded throws, the error will be
	//       caught and left on top of the stack for the caller to deal with.

	lstring_t*  code_string;
	path_t*     dir_path;
	path_t*     file_path;
	size_t      source_size;
	char*       source;

	file_path = path_new(filename);
	dir_path = path_strip(path_dup(file_path));

	// is the requested module already in the cache?
	duk_push_global_stash(ctx);
	duk_get_prop_string(ctx, -1, "moduleCache");
	if (duk_get_prop_string(ctx, -1, filename)) {
		duk_remove(ctx, -2);
		duk_remove(ctx, -2);
		goto have_module;
	}
	else {
		duk_pop_3(ctx);
	}

	source = fs_fslurp(fs, filename, &source_size);
	code_string = lstr_from_cp1252(source, source_size);
	free(source);

	// construct a module object for the new module
	duk_push_object(ctx);  // module object
	duk_push_object(ctx);
	duk_put_prop_string(ctx, -2, "exports");  // module.exports = {}
	duk_push_string(ctx, filename);
	duk_put_prop_string(ctx, -2, "filename");  // module.filename
	duk_push_string(ctx, filename);
	duk_put_prop_string(ctx, -2, "id");  // module.id
	duk_push_false(ctx);
	duk_put_prop_string(ctx, -2, "loaded");  // module.loaded = false
	push_require(ctx, filename);
	duk_put_prop_string(ctx, -2, "require");  // module.require

	// cache the module object in advance
	duk_push_global_stash(ctx);
	duk_get_prop_string(ctx, -1, "moduleCache");
	duk_dup(ctx, -3);
	duk_put_prop_string(ctx, -2, filename);
	duk_pop_2(ctx);

	if (strcmp(path_extension(file_path), ".json") == 0) {
		// JSON file, decode to JavaScript object
		duk_push_lstring_t(ctx, code_string);
		lstr_free(code_string);
		if (duk_json_pdecode(ctx) != DUK_EXEC_SUCCESS)
			goto on_error;
		duk_put_prop_string(ctx, -2, "exports");
	}
	else {
		// synthesize a function to wrap the module code.  this is the simplest way to
		// implement CommonJS semantics and matches the behavior of Node.js.
		if (!as_mjs) {
			duk_push_string(ctx, "(function(exports, require, module, __filename, __dirname) {");
			duk_push_string(ctx, strncmp(lstr_cstr(code_string), "#!", 2) == 0 ? "//" : "");  // shebang
			duk_push_lstring_t(ctx, code_string);
			duk_push_string(ctx, "\n})");
			duk_concat(ctx, 4);
			duk_push_string(ctx, filename);
		}
		else {
			duk_push_null(ctx);  // stack balancing
		}
		if (as_mjs || duk_pcompile(ctx, DUK_COMPILE_EVAL) != DUK_EXEC_SUCCESS) {
			// potentially ES 2015+ code; try transpiling it.
			// note: it might be better to eventually lift this functionality into a separate source file.
			//       it's pretty involved due to Duktape's stack API and is kind of an eyesore here.
			duk_push_global_stash(ctx);
			if (!duk_has_prop_string(ctx, -1, "TypeScript")) {
				duk_pop(ctx);
				goto on_error;  // no ES 2015 support
			}
			duk_get_prop_string(ctx, -1, "TypeScript");
			duk_get_prop_string(ctx, -1, "transpileModule");
			duk_swap_top(ctx, -2);
			duk_push_lstring_t(ctx, code_string);
			if (as_mjs) {
				duk_eval_string(ctx,
					"({ target: 1, module: 1, allowJs: true, downlevelIteration: true, newLine: 1,"
					"   noImplicitUseStrict: false })");
			}
			else {
				duk_eval_string(ctx,
					"({ target: 1, module: 1, allowJs: true, downlevelIteration: true, newLine: 1,"
					"   noImplicitUseStrict: true })");
			}
			if (duk_pcall_method(ctx, 2) != DUK_EXEC_SUCCESS) {
				duk_remove(ctx, -2);
				goto on_error;
			}
			duk_get_prop_string(ctx, -1, "outputText");
			lstr_free(code_string);
			code_string = duk_require_lstring_t(ctx, -1);
			duk_pop_n(ctx, 4);

			// try to compile it again.  if this attempt fails, it's a lost cause.
			duk_push_string(ctx, "(function(exports, require, module, __filename, __dirname) { ");
			duk_push_string(ctx, strncmp(lstr_cstr(code_string), "#!", 2) == 0 ? "//" : "");  // shebang
			duk_push_lstring_t(ctx, code_string);
			duk_push_string(ctx, " })");
			duk_concat(ctx, 4);
			duk_push_string(ctx, filename);
			if (duk_pcompile(ctx, DUK_COMPILE_EVAL) != DUK_EXEC_SUCCESS)
				goto on_error;
		}
		duk_call(ctx, 0);
		duk_push_string(ctx, "name");
		duk_push_string(ctx, "main");
		duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_FORCE);
		lstr_free(code_string);

		// go, go, go!
		duk_dup(ctx, -2);                           // this = module
		duk_get_prop_string(ctx, -3, "exports");    // exports
		duk_get_prop_string(ctx, -4, "require");    // require
		duk_dup(ctx, -5);                           // module
		duk_push_string(ctx, filename);             // __filename
		duk_push_string(ctx, path_cstr(dir_path));  // __dirname
		if (duk_pcall_method(ctx, 5) != DUK_EXEC_SUCCESS)
			goto on_error;
		duk_pop(ctx);
	}

	// module executed successfully, set `module.loaded` to true
	duk_push_true(ctx);
	duk_put_prop_string(ctx, -2, "loaded");

have_module:
	// `module` is on the stack, we need `module.exports`
	duk_get_prop_string(ctx, -1, "exports");
	duk_remove(ctx, -2);
	return 1;

on_error:
	// note: it's assumed that at this point, the only things left in our portion of the
	//       Duktape stack are the module object and the thrown error.
	duk_push_global_stash(ctx);
	duk_get_prop_string(ctx, -1, "moduleCache");
	duk_del_prop_string(ctx, -1, filename);
	duk_pop_2(ctx);
	duk_remove(ctx, -2);  // leave the error on the stack
	return 0;
}

static path_t*
find_cjs_module(duk_context* ctx, fs_t* fs, const char* id, const char* origin, const char* sys_origin)
{
	const char* const filenames[] =
	{
		"%s",
		"%s.mjs",
		"%s.js",
		"%s.json",
		"%s/package.json",
		"%s/index.mjs",
		"%s/index.js",
		"%s/index.json",
	};

	path_t*     origin_path;
	char*       filename;
	path_t*     main_path;
	path_t*     path;

	int i;

	if (strncmp(id, "./", 2) == 0 || strncmp(id, "../", 3) == 0)
		// resolve module relative to calling module
		origin_path = path_new(origin != NULL ? origin : "./");
	else
		// resolve module from designated module repository
		origin_path = path_new_dir(sys_origin);

	for (i = 0; i < (int)(sizeof(filenames) / sizeof(filenames[0])); ++i) {
		filename = strnewf(filenames[i], id);
		if (strncmp(id, "@/", 2) == 0 || strncmp(id, "$/", 2) == 0 || strncmp(id, "~/", 2) == 0 || strncmp(id, "#/", 2) == 0) {
			path = fs_full_path(filename, NULL);
		}
		else {
			path = path_dup(origin_path);
			path_strip(path);
			path_append(path, filename);
			path_collapse(path, true);
		}
		free(filename);
		if (fs_fexist(fs, path_cstr(path))) {
			if (strcmp(path_filename(path), "package.json") != 0) {
				return path;
			}
			else {
				if (!(main_path = load_package_json(ctx, path_cstr(path))))
					goto next_filename;
				if (fs_fexist(fs, path_cstr(main_path))) {
					path_free(path);
					return main_path;
				}
			}
		}

	next_filename:
		path_free(path);
	}

	return NULL;
}

static duk_ret_t
install_target(duk_context* ctx)
{
	// note: install targets never have more than one source because an individual
	//       target is constructed for each file installed.

	build_t*    build;
	int         result;
	const char* source_path;
	const char* target_path;

	build = duk_get_heap_udata(ctx);

	target_path = duk_require_string(ctx, 0);
	duk_get_prop_index(ctx, 1, 0);
	source_path = duk_require_string(ctx, -1);

	result = fs_fcopy(build->fs, target_path, source_path, true);
	if (result == 0)
		// touch file to prevent "target file unchanged" warning
		fs_utime(build->fs, target_path, NULL);
	duk_push_boolean(ctx, result == 0);
	return 1;
}

static path_t*
load_package_json(duk_context* ctx, const char* filename)
{
	build_t*    build;
	duk_idx_t   duk_top;
	char*       json;
	size_t      json_size;
	path_t*     path;

	build = duk_get_heap_udata(ctx);

	duk_top = duk_get_top(ctx);
	if (!(json = fs_fslurp(build->fs, filename, &json_size)))
		goto on_error;
	duk_push_lstring(ctx, json, json_size);
	free(json);
	if (duk_json_pdecode(ctx) != DUK_EXEC_SUCCESS)
		goto on_error;
	if (!duk_is_object_coercible(ctx, -1))
		goto on_error;
	duk_get_prop_string(ctx, -1, "main");
	if (!duk_is_string(ctx, -1))
		goto on_error;
	path = path_strip(path_new(filename));
	path_append(path, duk_get_string(ctx, -1));
	path_collapse(path, true);
	if (!fs_fexist(build->fs, path_cstr(path)))
		goto on_error;
	return path;

on_error:
	duk_set_top(ctx, duk_top);
	return NULL;
}

static void
make_file_targets(fs_t* fs, const char* wildcard, const path_t* path, const path_t* subdir, vector_t* targets, bool recursive, time_t timestamp)
{
	// note: 'targets' should be a vector_t initialized to sizeof(target_t*).

	path_t*      file_path;
	bool         ignore_dir;
	vector_t*    list;
	path_t*      name;
	target_t*    target;

	iter_t iter;
	path_t* *p_path;

	if (!(list = fs_list_dir(fs, path_cstr(path))))
		return;

	iter = vector_enum(list);
	while (p_path = iter_next(&iter)) {
		ignore_dir = fs_is_game_dir(fs, path_cstr(*p_path))
			&& path_num_hops(path) > 0
			&& !path_hop_is(path, 0, "@");
		if (!path_is_file(*p_path) && !ignore_dir && recursive) {
			name = path_new_dir(path_hop(*p_path, path_num_hops(*p_path) - 1));
			file_path = path_dup(*p_path);
			if (subdir != NULL)
				path_rebase(name, subdir);
			make_file_targets(fs, wildcard, file_path, name, targets, true, timestamp);
			path_free(file_path);
			path_free(name);
		}
		else if (path_is_file(*p_path) && wildcmp(path_filename(*p_path), wildcard)) {
			name = path_new(path_filename(*p_path));
			file_path = path_dup(*p_path);
			if (subdir != NULL)
				path_rebase(name, subdir);
			target = target_new(name, fs, file_path, NULL, timestamp, false);
			vector_push(targets, &target);
			path_free(file_path);
			path_free(name);
		}
	}

	iter = vector_enum(list);
	while (p_path = iter_next(&iter))
		path_free(*p_path);
	vector_free(list);
}

static void
push_require(duk_context* ctx, const char* module_id)
{
	duk_push_c_function(ctx, js_require, 1);
	duk_push_string(ctx, "name");
	duk_push_string(ctx, "require");
	duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE);  // require.name
	duk_push_string(ctx, "cache");
	duk_push_global_stash(ctx);
	duk_get_prop_string(ctx, -1, "moduleCache");
	duk_remove(ctx, -2);
	duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE);  // require.cache
	if (module_id != NULL) {
		duk_push_string(ctx, "id");
		duk_push_string(ctx, module_id);
		duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE);  // require.id
	}
}

static int
sort_targets_by_path(const void* p_a, const void* p_b)
{
	const target_t* a;
	const target_t* b;

	a = *(const target_t**)p_a;
	b = *(const target_t**)p_b;
	return strcmp(path_cstr(target_path(a)), path_cstr(target_path(b)));
}

static bool
write_manifests(build_t* build)
{
	duk_context* ctx;
	FILE*        file;
	int          height;
	size_t       json_size;
	const char*  json_text;
	path_t*      main_path;
	path_t*      script_path;
	int          width;

	ctx = build->js_context;

	visor_begin_op(build->visor, "writing Sphere manifest files");

	duk_push_global_stash(ctx);
	duk_get_prop_string(ctx, -1, "descriptor");

	// validate game descriptor before writing manifests
	duk_get_prop_string(ctx, -1, "name");
	if (!duk_is_string(ctx, -1)) {
		duk_push_string(ctx, "Untitled");
		duk_remove(ctx, -2);
		visor_warn(build->visor, "missing or invalid 'name' field");
	}

	duk_get_prop_string(ctx, -2, "author");
	if (!duk_is_string(ctx, -1)) {
		duk_push_string(ctx, "Author Unknown");
		duk_remove(ctx, -2);
		visor_warn(build->visor, "missing or invalid 'author' field");
	}

	duk_get_prop_string(ctx, -3, "summary");
	if (!duk_is_string(ctx, -1)) {
		duk_push_string(ctx, "No summary provided.");
		duk_remove(ctx, -2);
	}

	// note: SGMv1 encodes the resolution width and height as separate fields.
	duk_get_prop_string(ctx, -4, "resolution");
	if (!duk_is_string(ctx, -1)
		|| sscanf(duk_to_string(ctx, -1), "%dx%d", &width, &height) != 2)
	{
		visor_error(build->visor, "missing or invalid 'resolution' field");
		duk_pop_n(ctx, 6);
		visor_end_op(build->visor);
		return false;
	}

	duk_get_prop_string(ctx, -5, "main");
	if (duk_is_string(ctx, -1)) {
		// explicitly rebase onto `@/`, as Cell uses `$/` by default.
		main_path = fs_full_path(duk_to_string(ctx, -1), "@/");
		if (!path_hop_is(main_path, 0, "@")) {
			visor_error(build->visor, "'main': illegal prefix '%s/' in filename", path_hop(main_path, 0));
			duk_pop_n(ctx, 7);
			visor_end_op(build->visor);
			return false;
		}
		if (!fs_fexist(build->fs, path_cstr(main_path))) {
			visor_error(build->visor, "'main': file not found '%s'", path_cstr(main_path));
			duk_pop_n(ctx, 7);
			visor_end_op(build->visor);
			return false;
		}
		duk_push_string(ctx, path_cstr(main_path));
		duk_put_prop_string(ctx, -7, "main");
	}
	else {
		visor_error(build->visor, "missing or invalid 'main' field");
		duk_pop_n(ctx, 7);
		visor_end_op(build->visor);
		return false;
	}

	// write game.sgm (SGMv1, for compatibility with Sphere 1.x)
	// note: SGMv1 requires the main script path to be relative to `@/scripts`.
	//       this differs from Sv2 (game.json), where it's relative to `@/`.
	file = fs_fopen(build->fs, "@/game.sgm", "wb");
	script_path = fs_relative_path(path_cstr(main_path), "@/scripts");
	fprintf(file, "name=%s\n", duk_to_string(ctx, -5));
	fprintf(file, "author=%s\n", duk_to_string(ctx, -4));
	fprintf(file, "description=%s\n", duk_to_string(ctx, -3));
	fprintf(file, "screen_width=%d\n", width);
	fprintf(file, "screen_height=%d\n", height);
	fprintf(file, "script=%s\n", path_cstr(script_path));
	fclose(file);
	path_free(script_path);
	duk_pop_n(ctx, 5);

	// write game.json (Sphere v2 JSON manifest)
	duk_json_encode(ctx, -1);
	json_text = duk_get_lstring(ctx, -1, &json_size);
	fs_fspew(build->fs, "@/game.json", json_text, json_size);
	duk_pop_2(ctx);

	visor_end_op(build->visor);
	path_free(main_path);
	return true;
}

static duk_ret_t
js_error(duk_context* ctx)
{
	build_t*    build;
	const char* message;

	build = duk_get_heap_udata(ctx);

	message = duk_require_string(ctx, 0);

	visor_error(build->visor, "%s", message);
	return 0;
}

static duk_ret_t
js_files(duk_context* ctx)
{
	build_t*    build;
	int         num_args;
	const char* pattern;
	path_t*     path;
	bool        recursive = false;
	vector_t*   targets;
	char*       wildcard;

	iter_t iter;
	target_t* *p;

	build = duk_get_heap_udata(ctx);

	num_args = duk_get_top(ctx);
	pattern = duk_require_string(ctx, 0);
	if (num_args >= 2)
		recursive = duk_require_boolean(ctx, 1);

	// extract the wildcard, if any, from the given path
	path = path_new(pattern);
	if (!path_is_file(path))
		wildcard = strdup("*");
	else {
		wildcard = strdup(path_filename(path));
		path_strip(path);
	}

	// this is potentially recursive, so we defer to make_file_targets() to construct
	// the targets.  note: 'path' should always be a directory at this point.
	targets = vector_new(sizeof(target_t*));
	make_file_targets(build->fs, wildcard, path, NULL, targets, recursive, build->timestamp);
	free(wildcard);

	if (vector_len(targets) == 0)
		visor_warn(build->visor, "no existing files match '%s'", pattern);

	// return all the newly constructed targets as an array.
	duk_push_array(ctx);
	iter = vector_enum(targets);
	while (p = iter_next(&iter)) {
		duk_push_class_obj(ctx, "Target", *p);
		duk_put_prop_index(ctx, -2, (duk_uarridx_t)iter.index);
	}
	return 1;
}

static duk_ret_t
js_install(duk_context* ctx)
{
	build_t*      build;
	path_t*       dest_path;
	duk_uarridx_t length;
	target_t*     source;
	path_t*       name;
	path_t*       path;
	target_t*     target;
	tool_t*       tool;

	duk_uarridx_t i;

	build = duk_get_heap_udata(ctx);

	// retrieve the Install tool from the stash
	duk_push_global_stash(ctx);
	duk_get_prop_string(ctx, -1, "installTool");
	tool = duk_require_class_obj(ctx, -1, "Tool");
	duk_pop(ctx);

	dest_path = path_new_dir(duk_require_string(ctx, 0));

	if (duk_is_array(ctx, 1)) {
		length = (duk_uarridx_t)duk_get_length(ctx, 1);
		for (i = 0; i < length; ++i) {
			duk_get_prop_index(ctx, 1, i);
			source = duk_require_class_obj(ctx, -1, "Target");
			name = path_dup(target_name(source));
			path = path_rebase(path_dup(name), dest_path);
			target = target_new(name, build->fs, path, tool, build->timestamp, true);
			target_add_source(target, source);
			vector_push(build->targets, &target);
			duk_pop(ctx);
		}
	}
	else {
		source = duk_require_class_obj(ctx, 1, "Target");
		name = path_dup(target_name(source));
		path = path_rebase(path_dup(name), dest_path);
		target = target_new(name, build->fs, path, tool, build->timestamp, true);
		target_add_source(target, source);
		vector_push(build->targets, &target);
	}
	return 0;
}

static duk_ret_t
js_require(duk_context* ctx)
{
	const char* const PATHS[] =
	{
		"$/lib",
		"#/cell_modules",
		"#/runtime",
	};

	build_t*    build;
	const char* id;
	bool        is_mjs;
	const char* parent_id = NULL;
	path_t*     path;

	int i;

	build = duk_get_heap_udata(ctx);

	duk_push_current_function(ctx);
	if (duk_get_prop_string(ctx, -1, "id"))
		parent_id = duk_get_string(ctx, -1);
	id = duk_require_string(ctx, 0);

	if (parent_id == NULL && (strncmp(id, "./", 2) == 0 || strncmp(id, "../", 3) == 0))
		duk_error_blame(ctx, -1, DUK_ERR_TYPE_ERROR, "relative require not allowed in global code");

	for (i = 0; i < sizeof PATHS / sizeof PATHS[0]; ++i) {
		if (path = find_cjs_module(ctx, build->fs, id, parent_id, PATHS[i]))
			break;  // short-circuit
	}
	if (path == NULL)
		duk_error_blame(ctx, -1, DUK_ERR_REFERENCE_ERROR, "module not found '%s'", id);
	is_mjs = path_has_extension(path, ".mjs");
	if (!eval_cjs_module(ctx, build->fs, path_cstr(path), is_mjs))
		duk_throw(ctx);
	return 1;
}

static duk_ret_t
js_warn(duk_context* ctx)
{
	build_t*    build;
	const char* message;

	build = duk_get_heap_udata(ctx);

	message = duk_require_string(ctx, 0);

	visor_warn(build->visor, "%s", message);
	return 0;
}

static duk_ret_t
js_Sphere_get_Game(duk_context* ctx)
{
	duk_push_global_stash(ctx);
	duk_get_prop_string(ctx, -1, "descriptor");

	duk_push_this(ctx);
	duk_push_string(ctx, "Game");
	duk_dup(ctx, -3);
	duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE
		| DUK_DEFPROP_CLEAR_ENUMERABLE
		| DUK_DEFPROP_CLEAR_WRITABLE
		| DUK_DEFPROP_SET_CONFIGURABLE);
	duk_pop(ctx);

	return 1;
}

static duk_ret_t
js_Sphere_get_Platform(duk_context* ctx)
{
	duk_push_sprintf(ctx, "%s %s", COMPILER_NAME, VERSION_NAME);
	return 1;
}

static duk_ret_t
js_Sphere_get_Version(duk_context* ctx)
{
	duk_push_int(ctx, 2);
	return 1;
}

static duk_ret_t
js_new_DirectoryStream(duk_context* ctx)
{
	build_t*     build;
	const char*  pathname;
	directory_t* stream;

	if (!duk_is_constructor_call(ctx))
		duk_error_blame(ctx, -1, DUK_ERR_TYPE_ERROR, "constructor requires 'new'");

	build = duk_get_heap_udata(ctx);
	pathname = duk_require_pathname(ctx, 0, NULL);

	if (!(stream = directory_open(build->fs, pathname)))
		duk_error_blame(ctx, -1, DUK_ERR_ERROR, "couldn't open directory");
	duk_push_this(ctx);
	duk_to_class_obj(ctx, -1, "DirectoryStream", stream);
	return 0;
}

static duk_ret_t
js_DirectoryStream_finalize(duk_context* ctx)
{
	directory_t* stream;

	stream = duk_require_class_obj(ctx, 0, "DirectoryStream");
	directory_close(stream);
	return 0;
}

static duk_ret_t
js_DirectoryStream_get_fileCount(duk_context* ctx)
{
	directory_t* stream;

	duk_push_this(ctx);
	stream = duk_require_class_obj(ctx, -1, "DirectoryStream");

	duk_push_int(ctx, directory_num_files(stream));
	return 1;
}

static duk_ret_t
js_DirectoryStream_get_fileName(duk_context* ctx)
{
	directory_t* stream;

	duk_push_this(ctx);
	stream = duk_require_class_obj(ctx, -1, "DirectoryStream");

	duk_push_string(ctx, directory_pathname(stream));
	return 1;
}

static duk_ret_t
js_DirectoryStream_get_position(duk_context* ctx)
{
	directory_t* stream;

	duk_push_this(ctx);
	stream = duk_require_class_obj(ctx, -1, "DirectoryStream");

	duk_push_int(ctx, directory_position(stream));
	return 1;
}

static duk_ret_t
js_DirectoryStream_set_position(duk_context* ctx)
{
	int          position;
	directory_t* stream;

	duk_push_this(ctx);
	stream = duk_require_class_obj(ctx, -1, "DirectoryStream");
	position = duk_require_int(ctx, 0);

	if (!directory_seek(stream, position))
		duk_error_blame(ctx, -1, DUK_ERR_ERROR, "couldn't set stream position");
	return 0;
}

static duk_ret_t
js_DirectoryStream_next(duk_context* ctx)
{
	const path_t* entry_path;
	directory_t*  stream;

	duk_push_this(ctx);
	stream = duk_require_class_obj(ctx, -1, "DirectoryStream");

	entry_path = directory_next(stream);
	duk_push_object(ctx);
	if (entry_path != NULL) {
		duk_push_boolean(ctx, false);
		duk_put_prop_string(ctx, -2, "done");
		duk_push_object(ctx);
		if (path_is_file(entry_path))
			duk_push_string(ctx, path_filename(entry_path));
		else
			duk_push_sprintf(ctx, "%s/", path_hop(entry_path, path_num_hops(entry_path) - 1));
		duk_put_prop_string(ctx, -2, "fileName");
		duk_push_string(ctx, path_cstr(entry_path));
		duk_put_prop_string(ctx, -2, "fullPath");
		duk_push_boolean(ctx, !path_is_file(entry_path));
		duk_put_prop_string(ctx, -2, "isDirectory");
		duk_put_prop_string(ctx, -2, "value");
	}
	else {
		duk_push_boolean(ctx, true);
		duk_put_prop_string(ctx, -2, "done");
	}
	return 1;
}

static duk_ret_t
js_DirectoryStream_rewind(duk_context* ctx)
{
	directory_t* stream;

	duk_push_this(ctx);
	stream = duk_require_class_obj(ctx, -1, "DirectoryStream");

	directory_rewind(stream);
	return 0;
}

static duk_ret_t
js_FS_createDirectory(duk_context* ctx)
{
	build_t*    build;
	const char* name;

	build = duk_get_heap_udata(ctx);

	name = duk_require_pathname(ctx, 0, NULL);

	if (fs_mkdir(build->fs, name) != 0)
		duk_error_blame(ctx, -1, DUK_ERR_ERROR, "unable to create directory");
	return 0;
}

static duk_ret_t
js_FS_deleteFile(duk_context* ctx)
{
	build_t*    build;
	const char* filename;

	build = duk_get_heap_udata(ctx);

	filename = duk_require_pathname(ctx, 0, NULL);

	if (!fs_unlink(build->fs, filename))
		duk_error_blame(ctx, -1, DUK_ERR_ERROR, "unable to delete file", filename);
	return 0;
}

static duk_ret_t
js_FS_directoryExists(duk_context* ctx)
{
	build_t*    build;
	const char* dirname;

	build = duk_get_heap_udata(ctx);

	dirname = duk_require_pathname(ctx, 0, NULL);

	duk_push_boolean(ctx, fs_dir_exists(build->fs, dirname));
	return 1;
}

static duk_ret_t
js_FS_fileExists(duk_context* ctx)
{
	build_t*    build;
	const char* filename;

	build = duk_get_heap_udata(ctx);

	filename = duk_require_pathname(ctx, 0, NULL);

	duk_push_boolean(ctx, fs_fexist(build->fs, filename));
	return 1;
}

static duk_ret_t
js_FS_fullPath(duk_context* ctx)
{
	const char* filename;
	int         num_args;
	const char* origin_pathname = NULL;

	num_args = duk_get_top(ctx);
	if (num_args >= 2)
		origin_pathname = duk_require_pathname(ctx, 1, NULL);
	filename = duk_require_pathname(ctx, 0, origin_pathname);

	duk_push_string(ctx, filename);
	return 1;
}

static duk_ret_t
js_FS_readFile(duk_context* ctx)
{
	build_t*    build;
	lstring_t*  content;
	void*       file_data;
	size_t      file_size;
	const char* filename;

	build = duk_get_heap_udata(ctx);

	filename = duk_require_pathname(ctx, 0, NULL);

	if (!(file_data = fs_fslurp(build->fs, filename, &file_size)))
		duk_error_blame(ctx, -1, DUK_ERR_ERROR, "couldn't read file '%s'", filename);
	content = lstr_from_cp1252(file_data, file_size);
	duk_push_lstring_t(ctx, content);
	return 1;
}

static duk_ret_t
js_FS_relativePath(duk_context* ctx)
{
	const char* base_pathname;
	path_t*     path;
	const char* pathname;

	pathname = duk_require_pathname(ctx, 0, NULL);
	base_pathname = duk_require_pathname(ctx, 1, NULL);

	path = fs_relative_path(pathname, base_pathname);
	duk_push_string(ctx, path_cstr(path));
	path_free(path);
	return 1;
}

static duk_ret_t
js_FS_removeDirectory(duk_context* ctx)
{
	build_t*    build;
	const char* name;

	build = duk_get_heap_udata(ctx);

	name = duk_require_pathname(ctx, 0, NULL);

	if (!fs_rmdir(build->fs, name))
		duk_error_blame(ctx, -1, DUK_ERR_ERROR, "directory removal failed");
	return 0;
}

static duk_ret_t
js_FS_rename(duk_context* ctx)
{
	build_t*    build;
	const char* name1;
	const char* name2;

	build = duk_get_heap_udata(ctx);

	name1 = duk_require_pathname(ctx, 0, NULL);
	name2 = duk_require_pathname(ctx, 1, NULL);

	if (!fs_rename(build->fs, name1, name2))
		duk_error_blame(ctx, -1, DUK_ERR_ERROR, "rename failed", name1, name2);
	return 0;
}

static duk_ret_t
js_FS_writeFile(duk_context* ctx)
{
	build_t*    build;
	const char* filename;
	lstring_t*  text = NULL;

	build = duk_get_heap_udata(ctx);

	filename = duk_require_pathname(ctx, 0, NULL);
	text = duk_require_lstring_t(ctx, 1);

	if (!fs_fspew(build->fs, filename, lstr_cstr(text), lstr_len(text)))
		duk_error_blame(ctx, -1, DUK_ERR_ERROR, "couldn't write file '%s'", filename);
	lstr_free(text);
	return 0;
}

static duk_ret_t
js_new_FileStream(duk_context* ctx)
{
	build_t*     build;
	FILE*        file;
	enum file_op file_op;
	const char*  filename;
	const char*  mode;

	if (!duk_is_constructor_call(ctx))
		duk_error_blame(ctx, -1, DUK_ERR_TYPE_ERROR, "constructor requires 'new'");

	build = duk_get_heap_udata(ctx);
	duk_require_string(ctx, 0);
	file_op = duk_require_int(ctx, 1);
	if (file_op < 0 || file_op >= FILE_OP_MAX)
		duk_error_blame(ctx, -1, DUK_ERR_RANGE_ERROR, "invalid file-op constant");

	filename = duk_require_pathname(ctx, 0, NULL);
	if (file_op == FILE_OP_UPDATE && !fs_fexist(build->fs, filename))
		file_op = FILE_OP_WRITE;  // because 'r+b' requires the file to exist.
	mode = file_op == FILE_OP_READ ? "rb"
		: file_op == FILE_OP_WRITE ? "w+b"
		: file_op == FILE_OP_UPDATE ? "r+b"
		: NULL;
	if (!(file = fs_fopen(build->fs, filename, mode)))
		duk_error_blame(ctx, -1, DUK_ERR_ERROR, "couldn't open file `%s`", filename);
	if (file_op == FILE_OP_UPDATE)
		fseek(file, 0, SEEK_END);
	duk_push_this(ctx);
	duk_to_class_obj(ctx, -1, "FileStream", file);
	return 0;
}

static duk_ret_t
js_FileStream_finalize(duk_context* ctx)
{
	FILE* file;

	file = duk_require_class_obj(ctx, 0, "FileStream");

	if (file != NULL)
		fclose(file);
	return 0;
}

static duk_ret_t
js_FileStream_get_position(duk_context* ctx)
{
	FILE* file;

	duk_push_this(ctx);
	if (!(file = duk_require_class_obj(ctx, -1, "FileStream")))
		duk_error_blame(ctx, -1, DUK_ERR_ERROR, "use of disposed object");

	duk_push_number(ctx, ftell(file));
	return 1;
}

static duk_ret_t
js_FileStream_get_fileSize(duk_context* ctx)
{
	FILE* file;
	long  file_pos;

	duk_push_this(ctx);
	if (!(file = duk_require_class_obj(ctx, -1, "FileStream")))
		duk_error_blame(ctx, -1, DUK_ERR_ERROR, "use of disposed object");

	file_pos = ftell(file);
	fseek(file, 0, SEEK_END);
	duk_push_number(ctx, ftell(file));
	fseek(file, file_pos, SEEK_SET);
	return 1;
}

static duk_ret_t
js_FileStream_set_position(duk_context* ctx)
{
	FILE* file;
	long  new_pos;

	duk_push_this(ctx);
	if (!(file = duk_require_class_obj(ctx, -1, "FileStream")))
		duk_error_blame(ctx, -1, DUK_ERR_ERROR, "use of disposed object");
	new_pos = duk_require_int(ctx, 0);

	if (new_pos < 0)
		duk_error_blame(ctx, -1, DUK_ERR_RANGE_ERROR, "invalid file position");
	fseek(file, new_pos, SEEK_SET);
	return 0;
}

static duk_ret_t
js_FileStream_dispose(duk_context* ctx)
{
	FILE* file;

	duk_push_this(ctx);
	file = duk_require_class_obj(ctx, -1, "FileStream");

	duk_push_pointer(ctx, NULL);
	duk_put_prop_string(ctx, -2, "\xFF" "udata");
	if (file != NULL)
		fclose(file);
	return 0;
}

static duk_ret_t
js_FileStream_read(duk_context* ctx)
{
	// FileStream:read([numBytes]);
	// Reads data from the stream, returning it in an ArrayBuffer.
	// Arguments:
	//     numBytes: Optional. The number of bytes to read. If not provided, the
	//               entire file is read.

	int    argc;
	void*  buffer;
	FILE*  file;
	int    num_bytes;
	long   pos;

	argc = duk_get_top(ctx);
	num_bytes = argc >= 1 ? duk_require_int(ctx, 0) : 0;

	duk_push_this(ctx);
	if (!(file = duk_require_class_obj(ctx, -1, "FileStream")))
		duk_error_blame(ctx, -1, DUK_ERR_ERROR, "use of disposed object");
	if (argc < 1) {  // if no arguments, read entire file back to front
		pos = ftell(file);
		num_bytes = (fseek(file, 0, SEEK_END), ftell(file));
		fseek(file, 0, SEEK_SET);
	}
	if (num_bytes < 0)
		duk_error_blame(ctx, -1, DUK_ERR_RANGE_ERROR, "invalid read size");
	buffer = duk_push_fixed_buffer(ctx, num_bytes);
	num_bytes = (int)fread(buffer, 1, num_bytes, file);
	if (argc < 1)  // reset file position after whole-file read
		fseek(file, pos, SEEK_SET);
	duk_push_buffer_object(ctx, -1, 0, num_bytes, DUK_BUFOBJ_ARRAYBUFFER);
	return 1;
}

static duk_ret_t
js_FileStream_write(duk_context* ctx)
{
	const void* data;
	FILE*       file;
	duk_size_t  num_bytes;

	duk_push_this(ctx);
	if (!(file = duk_require_class_obj(ctx, -1, "FileStream")))
		duk_error_blame(ctx, -1, DUK_ERR_ERROR, "use of disposed object");
	data = duk_require_buffer_data(ctx, 0, &num_bytes);

	if (fwrite(data, 1, num_bytes, file) != num_bytes)
		duk_error_blame(ctx, -1, DUK_ERR_ERROR, "failure to write to file");
	return 0;
}

static duk_ret_t
js_RNG_fromSeed(duk_context* ctx)
{
	uint64_t seed;
	xoro_t*  xoro;

	seed = (uint64_t)duk_require_number(ctx, 0);

	xoro = xoro_new(seed);
	duk_push_class_obj(ctx, "RNG", xoro);
	return 1;
}

static duk_ret_t
js_RNG_fromState(duk_context* ctx)
{
	const char* state;
	xoro_t*     xoro;

	state = duk_require_string(ctx, 0);

	xoro = xoro_new(0);
	if (!xoro_set_state(xoro, state)) {
		xoro_unref(xoro);
		duk_error_blame(ctx, -1, DUK_ERR_TYPE_ERROR, "invalid RNG state string");
	}
	duk_push_class_obj(ctx, "RNG", xoro);
	return 1;
}

static duk_ret_t
js_new_RNG(duk_context* ctx)
{
	xoro_t* xoro;

	if (!duk_is_constructor_call(ctx))
		duk_error_blame(ctx, -1, DUK_ERR_TYPE_ERROR, "constructor requires 'new'");

	xoro = xoro_new((uint64_t)clock());
	duk_push_class_obj(ctx, "RNG", xoro);
	return 1;
}

static duk_ret_t
js_RNG_finalize(duk_context* ctx)
{
	xoro_t* xoro;

	xoro = duk_require_class_obj(ctx, 0, "RNG");

	xoro_unref(xoro);
	return 0;
}

static duk_ret_t
js_RNG_get_state(duk_context* ctx)
{
	char    state[33];
	xoro_t* xoro;

	duk_push_this(ctx);
	xoro = duk_require_class_obj(ctx, -1, "RNG");

	xoro_get_state(xoro, state);
	duk_push_string(ctx, state);
	return 1;
}

static duk_ret_t
js_RNG_set_state(duk_context* ctx)
{
	const char* state;
	xoro_t*     xoro;

	duk_push_this(ctx);
	xoro = duk_require_class_obj(ctx, -1, "RNG");
	state = duk_require_string(ctx, 0);

	if (!xoro_set_state(xoro, state))
		duk_error_blame(ctx, -1, DUK_ERR_TYPE_ERROR, "invalid RNG state string");
	return 0;
}

static duk_ret_t
js_RNG_next(duk_context* ctx)
{
	xoro_t*     xoro;

	duk_push_this(ctx);
	xoro = duk_require_class_obj(ctx, -1, "RNG");

	duk_push_number(ctx, xoro_gen_double(xoro));
	return 1;
}

static duk_ret_t
js_Target_finalize(duk_context* ctx)
{
	target_t* target;

	target = duk_require_class_obj(ctx, 0, "Target");

	target_free(target);
	return 0;
}

static duk_ret_t
js_Target_get_fileName(duk_context* ctx)
{
	target_t* target;

	duk_push_this(ctx);
	target = duk_require_class_obj(ctx, -1, "Target");

	duk_push_string(ctx, path_cstr(target_path(target)));
	return 1;
}

static duk_ret_t
js_Target_get_name(duk_context* ctx)
{
	target_t* target;

	duk_push_this(ctx);
	target = duk_require_class_obj(ctx, -1, "Target");

	duk_push_string(ctx, path_cstr(target_name(target)));
	return 1;
}

static duk_ret_t
js_new_Tool(duk_context* ctx)
{
	int         num_args;
	tool_t*     tool;
	const char* verb = "building";

	num_args = duk_get_top(ctx);
	if (!duk_is_constructor_call(ctx))
		duk_error_blame(ctx, -1, DUK_ERR_TYPE_ERROR, "constructor requires 'new'");
	duk_require_function(ctx, 0);
	if (num_args >= 2)
		verb = duk_require_string(ctx, 1);

	duk_dup(ctx, 0);
	tool = tool_new(ctx, verb);
	duk_push_this(ctx);
	duk_to_class_obj(ctx, -1, "Tool", tool);
	return 1;
}

static duk_ret_t
js_Tool_finalize(duk_context* ctx)
{
	tool_t* tool;

	tool = duk_require_class_obj(ctx, 0, "Tool");

	tool_unref(tool);
	return 0;
}

static duk_ret_t
js_Tool_stage(duk_context* ctx)
{
	build_t*      build;
	duk_uarridx_t length;
	path_t*       name;
	int           num_args;
	path_t*       out_path;
	target_t*     source;
	target_t*     target;
	tool_t*       tool;

	duk_uarridx_t i;

	build = duk_get_heap_udata(ctx);

	num_args = duk_get_top(ctx);
	duk_push_this(ctx);
	tool = duk_require_class_obj(ctx, -1, "Tool");
	out_path = path_new(duk_require_string(ctx, 0));
	if (!duk_is_array(ctx, 1))
		duk_error_blame(ctx, -1, DUK_ERR_TYPE_ERROR, "array required (argument #2)");
	if (num_args >= 3)
		duk_require_object_coercible(ctx, 2);

	name = path_new(path_filename(out_path));
	if (num_args >= 3) {
		if (duk_get_prop_string(ctx, 2, "name")) {
			path_free(name);
			name = path_new(duk_require_string(ctx, -1));
		}
		duk_pop_n(ctx, 1);
	}

	target = target_new(name, build->fs, out_path, tool, build->timestamp, true);
	length = (duk_uarridx_t)duk_get_length(ctx, 1);
	for (i = 0; i < length; ++i) {
		duk_get_prop_index(ctx, 1, i);
		source = duk_require_class_obj(ctx, -1, "Target");
		target_add_source(target, source);
		duk_pop(ctx);
	}
	path_free(out_path);
	vector_push(build->targets, &target);

	duk_push_class_obj(ctx, "Target", target_ref(target));
	return 1;
}
