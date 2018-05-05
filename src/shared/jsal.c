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

#ifdef _MSC_VER
#define _CRT_NONSTDC_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "jsal.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>

#include <ChakraCore.h>
#include "vector.h"

#if !defined(WIN32)
#define jsal_setjmp(env)          sigsetjmp(env, false)
#define jsal_longjmp(env, value)  siglongjmp(env, value)
#define jsal_jmpbuf               sigjmp_buf
#else
#define jsal_setjmp(env)          setjmp(env)
#define jsal_longjmp(env, value)  longjmp(env, value)
#define jsal_jmpbuf               jmp_buf
#endif

struct js_ref
{
	bool  weak_ref;
	JsRef value;
};

struct breakpoint
{
	int          column;
	char*        filename;
	unsigned int id;
	int          line;
};

struct function
{
	js_function_t callback;
	bool          ctor_only;
	int           magic;
	int           min_args;
};

struct module
{
	char*          filename;
	JsModuleRecord record;
};

struct module_job
{
	JsModuleRecord  module_record;
	char*           source;
	JsSourceContext source_context;
	size_t          source_size;
};

struct object
{
	void*          data;
	js_finalizer_t finalizer;
	JsValueRef     object;
};

static void CHAKRA_CALLBACK        on_debugger_event         (JsDiagDebugEvent event_type, JsValueRef data, void* userdata);
static JsErrorCode CHAKRA_CALLBACK on_fetch_dynamic_import   (JsSourceContext importer, JsValueRef specifier, JsModuleRecord *out_module);
static JsErrorCode CHAKRA_CALLBACK on_fetch_imported_module  (JsModuleRecord importer, JsValueRef specifier, JsModuleRecord *out_module);
static void CHAKRA_CALLBACK        on_finalize_host_object   (void* userdata);
static JsValueRef CHAKRA_CALLBACK  on_js_to_native_call      (JsValueRef callee, bool is_ctor, JsValueRef argv[], unsigned short argc, void* userdata);
static JsErrorCode CHAKRA_CALLBACK on_notify_module_ready    (JsModuleRecord module, JsValueRef exception);
static void CHAKRA_CALLBACK        on_resolve_reject_promise (JsValueRef function, void* userdata);
static const char*                 filename_from_script_id   (unsigned int script_id);
static void                        free_ref                  (js_ref_t* ref);
static JsModuleRecord              get_module_record         (const char* filename, JsModuleRecord parent, bool *out_is_new);
static JsValueRef                  get_value                 (int stack_index);
static JsPropertyIdRef             make_property_id          (JsValueRef key_value);
static js_ref_t*                   make_ref                  (JsRef value, bool weak_ref);
static JsValueRef                  pop_value                 (void);
static void                        push_debug_callback_args  (JsValueRef event_data);
static unsigned int                script_id_from_filename   (const char* filename);
static int                         push_value                (JsValueRef value, bool weak_ref);
static void                        resize_stack              (int new_size);
static void                        throw_on_error            (void);
static void                        throw_value               (JsValueRef value);

#if !defined(__APPLE__)
static int asprintf  (char* *out, const char* format, ...);
static int vasprintf (char* *out, const char* format, va_list ap);
#endif

static js_break_callback_t  s_break_callback = NULL;
static vector_t*            s_breakpoints;
static JsValueRef           s_callee_value = JS_INVALID_REFERENCE;
static vector_t*            s_catch_stack;
static js_import_callback_t s_import_callback = NULL;
static js_job_callback_t    s_job_callback = NULL;
static JsContextRef         s_js_context;
static JsRuntimeHandle      s_js_runtime = NULL;
static vector_t*            s_module_cache;
static vector_t*            s_module_jobs;
static JsSourceContext      s_next_source_context = 1;
static int                  s_stack_base;
static JsValueRef           s_stash;
static JsValueRef           s_this_value = JS_INVALID_REFERENCE;
static vector_t*            s_value_stack;
static js_throw_callback_t  s_throw_callback = NULL;

bool
jsal_init(void)
{
	JsModuleRecord module_record;
	JsErrorCode    result;
	
	result = JsCreateRuntime(
		JsRuntimeAttributeAllowScriptInterrupt
			| JsRuntimeAttributeDispatchSetExceptionsToDebugger
			| JsRuntimeAttributeEnableExperimentalFeatures,
		NULL, &s_js_runtime);
	if (result != JsNoError)
		goto on_error;
	if (JsCreateContext(s_js_runtime, &s_js_context) != JsNoError)
		goto on_error;
	JsSetCurrentContext(s_js_context);
	
	// set up the callbacks
	JsSetPromiseContinuationCallback(on_resolve_reject_promise, NULL);
	JsInitializeModuleRecord(NULL, NULL, &module_record);
	JsSetModuleHostInfo(module_record, JsModuleHostInfo_FetchImportedModuleCallback, on_fetch_imported_module);
	JsSetModuleHostInfo(module_record, JsModuleHostInfo_FetchImportedModuleFromScriptCallback, on_fetch_dynamic_import);
	JsSetModuleHostInfo(module_record, JsModuleHostInfo_NotifyModuleReadyCallback, on_notify_module_ready);

	// set up the stash, used to store JS values behind the scenes.
	JsCreateObject(&s_stash);
	JsAddRef(s_stash, NULL);

	s_value_stack = vector_new(sizeof(js_ref_t));
	s_catch_stack = vector_new(sizeof(jsal_jmpbuf));
	s_stack_base = 0;
	s_breakpoints = vector_new(sizeof(struct breakpoint));
	s_module_cache = vector_new(sizeof(struct module));
	s_module_jobs = vector_new(sizeof(struct module_job));

	vector_reserve(s_value_stack, 128);
	vector_reserve(s_catch_stack, 32);

	return true;

on_error:
	if (s_js_runtime != NULL)
		JsDisposeRuntime(s_js_runtime);
	s_js_runtime = NULL;
	return false;
}

void
jsal_uninit(void)
{
	struct breakpoint* breakpoint;
	struct module*     module;

	iter_t iter;

	iter = vector_enum(s_breakpoints);
	while (iter_next(&iter)) {
		breakpoint = iter.ptr;
		free(breakpoint->filename);
	}
	
	iter = vector_enum(s_module_cache);
	while (module = iter_next(&iter)) {
		JsRelease(module->record, NULL);
		free(module->filename);
	}
	
	// clear value stack, releasing all references
	resize_stack(0);
	
	vector_free(s_breakpoints);
	vector_free(s_module_cache);
	vector_free(s_module_jobs);
	vector_free(s_value_stack);
	vector_free(s_catch_stack);
	JsRelease(s_stash, NULL);
	JsSetCurrentContext(JS_INVALID_REFERENCE);
	JsDisposeRuntime(s_js_runtime);
}

bool
jsal_reinit(void)
{
	struct breakpoint* breakpoint;
	JsContextRef       context;
	struct module*     module;
	JsModuleRecord     module_record;

	iter_t iter;

	if (JsCreateContext(s_js_runtime, &context) != JsNoError)
		return false;

	resize_stack(0);
	JsRelease(s_stash, NULL);

	iter = vector_enum(s_breakpoints);
	while (iter_next(&iter)) {
		breakpoint = iter.ptr;
		free(breakpoint->filename);
	}

	iter = vector_enum(s_module_cache);
	while (module = iter_next(&iter)) {
		JsRelease(module->record, NULL);
		free(module->filename);
	}

	vector_resize(s_breakpoints, 0);
	vector_resize(s_catch_stack, 0);
	vector_resize(s_module_cache, 0);
	vector_resize(s_module_jobs, 0);

	JsSetCurrentContext(context);
	JsCreateObject(&s_stash);
	JsAddRef(s_stash, NULL);

	// callbacks must be reinitialized for the new context.
	JsSetPromiseContinuationCallback(on_resolve_reject_promise, NULL);
	JsInitializeModuleRecord(NULL, NULL, &module_record);
	JsSetModuleHostInfo(module_record, JsModuleHostInfo_FetchImportedModuleCallback, on_fetch_imported_module);
	JsSetModuleHostInfo(module_record, JsModuleHostInfo_FetchImportedModuleFromScriptCallback, on_fetch_dynamic_import);
	JsSetModuleHostInfo(module_record, JsModuleHostInfo_NotifyModuleReadyCallback, on_notify_module_ready);

	s_callee_value = JS_INVALID_REFERENCE;
	s_js_context = context;
	s_next_source_context = 1;
	s_this_value = JS_INVALID_REFERENCE;
	s_stack_base = 0;

	return true;
}

void
jsal_update(bool in_event_loop)
{
	JsErrorCode        error_code;
	JsValueRef         exception;
	bool               have_error;
	struct module_job* job;
	JsModuleRecord     module_record;
	JsValueRef         result;
	char*              source;

	// we're not loading remote modules, so there's no sense causing delays by doing
	// one at a time.  just blow through the whole dependency graph in one tick.
	while (vector_len(s_module_jobs) > 0) {
		job = vector_get(s_module_jobs, 0);
		if (job->source != NULL) {
			// module parse job: parse an imported module
			source = job->source;  // ...because 'job' may be invalidated
			error_code = JsParseModuleSource(job->module_record, job->source_context,
				(BYTE*)job->source, (unsigned int)job->source_size,
				JsParseModuleSourceFlags_DataIsUTF8, &exception);
			free(source);
			vector_remove(s_module_jobs, 0);
		}
		else {
			// module evaluation job: execute a top-level module.  this is tricky because
			// a module may call Sphere.run() at load time.  in order to avoid corrupting
			// the queue or doing the evaluation more than once, dequeue the job first.
			module_record = job->module_record;
			vector_remove(s_module_jobs, 0);
			JsModuleEvaluation(module_record, &result);
			if (!in_event_loop)
				throw_on_error();
			JsHasException(&have_error);
			if (have_error) {
				JsGetAndClearException(&exception);
				JsSetModuleHostInfo(module_record, JsModuleHostInfo_Exception, exception);
			}
		}
	}
}

bool
jsal_busy(void)
{
	return vector_len(s_module_jobs) > 0;
}

bool
jsal_disabled(void)
{
	bool disabled;

	JsIsRuntimeExecutionDisabled(s_js_runtime, &disabled);
	return disabled;
}

void
jsal_on_enqueue_job(js_job_callback_t callback)
{
	s_job_callback = callback;
}

void
jsal_on_import_module(js_import_callback_t callback)
{
	s_import_callback = callback;
}

void
jsal_call(int num_args)
{
	/* [ ... function arg1..argN ] -> [ ... retval ] */

	jsal_push_undefined();
	if (num_args > 0)
		jsal_insert(-num_args - 1);
	jsal_call_method(num_args);
}

void
jsal_call_method(int num_args)
{
	/* [ ... function this arg1..argN ] -> [ ... retval ] */

	JsValueRef* arguments;
	JsValueRef  function_ref;
	int         offset;
	JsValueRef  retval_ref;

	int i;

	num_args += 1;  // treat 'this' as first argument
	arguments = malloc(num_args * sizeof(JsValueRef));
	function_ref = get_value(-num_args - 1);
	offset = -num_args;
	for (i = 0; i < num_args; ++i)
		arguments[i] = get_value(i + offset);
	JsCallFunction(function_ref, arguments, (unsigned short)num_args, &retval_ref);
	jsal_pop(num_args + 1);
	throw_on_error();
	push_value(retval_ref, false);
}

unsigned int
jsal_compile(const char* filename)
{
	/* [ ... source ] -> [ ... function ] */

	JsValueRef      function;
	JsValueRef      name_string;
	JsValueRef      source_string;
	
	source_string = pop_value();
	JsCreateString(filename, strlen(filename), &name_string);
	JsParse(source_string, s_next_source_context, name_string, JsParseScriptAttributeNone, &function);
	throw_on_error();
	push_value(function, false);
	return (unsigned int)s_next_source_context++;
}

void
jsal_construct(int num_args)
{
	/* [ ... constructor arg1..argN ] -> [ ... retval ] */

	JsValueRef* arguments;
	JsValueRef  function_ref;
	int         offset;
	JsValueRef  retval_ref;
	JsValueRef  undefined;

	int i;

	arguments = malloc((num_args + 1) * sizeof(JsValueRef));
	function_ref = get_value(-num_args - 1);
	offset = -num_args;
	JsGetUndefinedValue(&undefined);
	arguments[0] = undefined;
	for (i = 0; i < num_args; ++i)
		arguments[i + 1] = get_value(i + offset);
	JsConstructObject(function_ref, arguments, (unsigned short)(num_args + 1), &retval_ref);
	jsal_pop(num_args + 1);
	throw_on_error();
	push_value(retval_ref, false);
}

void
jsal_def_prop(int object_index)
{
	/* [ ... key descriptor ] -> [ ... ] */

	JsValueRef      descriptor;
	JsPropertyIdRef key;
	JsValueRef      object;
	bool            result;

	object = get_value(object_index);
	descriptor = pop_value();
	key = make_property_id(pop_value());
	JsDefineProperty(object, key, descriptor, &result);
	throw_on_error();
}

void
jsal_def_prop_index(int object_index, int name)
{
	/* [ ... descriptor ] -> [ ... ] */

	object_index = jsal_normalize_index(object_index);
	jsal_push_sprintf("%d", name);
	jsal_insert(-2);
	jsal_def_prop(object_index);
}

void
jsal_def_prop_string(int object_index, const char* name)
{
	/* [ ... descriptor ] -> [ ... ] */

	object_index = jsal_normalize_index(object_index);
	jsal_push_string(name);
	jsal_insert(-2);
	jsal_def_prop(object_index);
}

bool
jsal_del_global(void)
{
	/* [ ... key ] -> [ ... ] */

	JsPropertyIdRef key_ref;
	JsValueRef      object_ref;
	JsValueRef      result;
	bool            retval;

	JsGetGlobalObject(&object_ref);
	key_ref = make_property_id(pop_value());
	JsDeleteProperty(object_ref, key_ref, true, &result);
	throw_on_error();
	JsBooleanToBool(result, &retval);
	return retval;
}

bool
jsal_del_global_string(const char* name)
{
	jsal_push_string(name);
	return jsal_del_global();
}

bool
jsal_del_prop(int object_index)
{
	/* [ ... key ] -> [ ... ] */

	JsPropertyIdRef key_ref;
	JsValueRef      object_ref;
	JsValueRef      result;
	bool            retval;

	object_ref = get_value(object_index);
	key_ref = make_property_id(pop_value());
	JsDeleteProperty(object_ref, key_ref, true, &result);
	throw_on_error();
	JsBooleanToBool(result, &retval);
	return retval;
}

bool
jsal_del_prop_index(int object_index, int name)
{
	object_index = jsal_normalize_index(object_index);
	jsal_push_sprintf("%d", name);
	return jsal_del_prop(object_index);
}

bool
jsal_del_prop_string(int object_index, const char* name)
{
	object_index = jsal_normalize_index(object_index);
	jsal_push_string(name);
	return jsal_del_prop(object_index);
}

void
jsal_disable(bool disabled)
{
	if (disabled)
		JsDisableRuntimeExecution(s_js_runtime);
	else
		JsEnableRuntimeExecution(s_js_runtime);
}

int
jsal_dup(int from_index)
{
	JsValueRef value;

	value = get_value(from_index);
	return push_value(value, false);
}

bool
jsal_equal(int at_index, int to_index)
{
	JsValueRef a, b;
	bool       result;

	a = get_value(at_index);
	b = get_value(to_index);
	JsStrictEquals(a, b, &result);
	return result;
}

void
jsal_error(js_error_type_t type, const char* format, ...)
{
	va_list ap;

	va_start(ap, format);
	jsal_error_va(type, format, ap);
	va_end(ap);
}

void
jsal_error_va(js_error_type_t type, const char* format, va_list ap)
{
	jsal_push_new_error_va(type, format, ap);
	jsal_throw();
}

void
jsal_eval_module(const char* filename)
{
	/* [ ... source ] -> [ ... result ] */

	JsErrorCode    error_code;
	JsValueRef     exception;
	bool           is_new_module;
	JsModuleRecord module;
	const char*    source;
	size_t         source_len;
	JsValueRef     url_string;

	source = jsal_require_lstring(-1, &source_len);
	JsCreateString(filename, strlen(filename), &url_string);
	module = get_module_record(filename, NULL, &is_new_module);
	if (is_new_module) {
		error_code = JsParseModuleSource(module,
			s_next_source_context++, (BYTE*)source, (unsigned int)source_len,
			JsParseModuleSourceFlags_DataIsUTF8, &exception);
		if (error_code == JsErrorScriptCompile)
			goto on_exception;
	}
	
	// note: a single call to jsal_update() here is enough, as it will process
	//       the entire dependency graph before returning.
	jsal_update(false);
	
	JsGetModuleHostInfo(module, JsModuleHostInfo_Exception, &exception);
	if (exception != JS_INVALID_REFERENCE)
		throw_value(exception);

	jsal_push_undefined();
	jsal_remove(-2);
	return;

on_exception:
	vector_clear(s_module_jobs);
	throw_on_error();
	throw_value(exception);
}

void
jsal_gc(void)
{
	JsCollectGarbage(s_js_runtime);
}

bool
jsal_get_boolean(int at_index)
{
	bool       value;
	JsValueRef value_ref;

	value_ref = get_value(at_index);
	if (JsBooleanToBool(value_ref, &value) != JsNoError)
		return false;
	return value;
}

void*
jsal_get_buffer_ptr(int at_index, size_t *out_size)
{
	unsigned int  size;
	JsValueType   type;
	ChakraBytePtr value;
	JsValueRef    value_ref;

	value_ref = get_value(at_index);
	JsGetValueType(value_ref, &type);
	if (type == JsTypedArray)
		JsGetTypedArrayStorage(value_ref, &value, &size, NULL, NULL);
	else if (type == JsArrayBuffer)
		JsGetArrayBufferStorage(value_ref, &value, &size);
	else
		return NULL;
	if (out_size != NULL)
		*out_size = size;
	return value;
}

bool
jsal_get_global(void)
{
	/* [ ... key ] -> [ ... value ] */

	JsPropertyIdRef key;
	JsValueRef      object;
	JsValueRef      value;

	key = make_property_id(pop_value());
	JsGetGlobalObject(&object);
	JsGetProperty(object, key, &value);
	throw_on_error();
	push_value(value, false);
	return !jsal_is_undefined(-1);
}

bool
jsal_get_global_string(const char* name)
{
	/* [ ... ] -> [ ... value ] */

	jsal_push_string(name);
	return jsal_get_global();
}

void*
jsal_get_host_data(int at_index)
{
	JsValueRef     object;
	struct object* object_info;
	void*          ptr;

	object = get_value(at_index);
	JsValueType type; JsGetValueType(object, &type);
	JsErrorCode ec = JsGetExternalData(object, &ptr);
	if (ec != JsNoError)
		return NULL;
	object_info = ptr;
	return object_info->data;
}

int
jsal_get_int(int index)
{
	int        value;
	JsValueRef value_ref;

	value_ref = get_value(index);
	if (JsNumberToInt(value_ref, &value) != JsNoError)
		return 0;
	return value;
}

int
jsal_get_length(int at_index)
{
	int value;

	jsal_get_prop_string(at_index, "length");
	value = jsal_get_int(-1);
	jsal_pop(1);
	return value;
}

const char*
jsal_get_lstring(int index, size_t *out_length)
{
	static int   counter = 0;
	static char* retval[25];

	char*       buffer;
	size_t      buffer_size;
	size_t      num_bytes;
	int         length;
	JsValueRef  value;

	value = get_value(index);
	if (JsGetStringLength(value, &length) != JsNoError)
		return NULL;
	buffer_size = length * 3 + 1;
	buffer = malloc(buffer_size);
	JsCopyString(value, buffer, buffer_size, &num_bytes);
	buffer[num_bytes] = '\0';  // NUL terminator
	free(retval[counter]);
	retval[counter] = buffer;
	counter = (counter + 1) % 25;
	if (out_length != NULL)
		*out_length = length;
	return buffer;
}

double
jsal_get_number(int index)
{
	double     value;
	JsValueRef value_ref;

	value_ref = get_value(index);
	if (JsNumberToDouble(value_ref, &value) != JsNoError)
		return NAN;
	return value;
}

bool
jsal_get_prop(int object_index)
{
	/* [ ... key ] -> [ ... value ] */

	JsPropertyIdRef key;
	JsValueRef      key_value;
	JsValueRef      object;
	JsValueRef      value;

	object = get_value(object_index);
	if (jsal_is_number(-1)) {
		key_value = pop_value();
		JsGetIndexedProperty(object, key_value, &value);
	}
	else {
		key = make_property_id(pop_value());
		JsGetProperty(object, key, &value);
	}
	throw_on_error();
	push_value(value, false);
	return !jsal_is_undefined(-1);
}

bool
jsal_get_prop_index(int object_index, int name)
{
	/* [ ... ] -> [ ... value ] */

	JsValueRef index;
	JsValueRef object;
	JsValueRef value;

	object = get_value(object_index);
	JsIntToNumber(name, &index);
	JsGetIndexedProperty(object, index, &value);
	throw_on_error();
	push_value(value, false);
	return !jsal_is_undefined(-1);
}

bool
jsal_get_prop_key(int object_index, js_ref_t* key)
{
	/* [ ... ] -> [ ... value ] */

	JsValueRef      object;
	JsValueRef      value;

	object = get_value(object_index);
	JsGetProperty(object, key->value, &value);
	throw_on_error();
	push_value(value, false);
	return !jsal_is_undefined(-1);
}

bool
jsal_get_prop_string(int object_index, const char* name)
{
	/* [ ... ] -> [ ... value ] */

	JsPropertyIdRef key;
	JsValueRef      object;
	JsValueRef      value;

	object = get_value(object_index);
	JsCreatePropertyId(name, strlen(name), &key);
	JsGetProperty(object, key, &value);
	throw_on_error();
	push_value(value, false);
	return !jsal_is_undefined(-1);
}

void
jsal_get_prototype(int object_index)
{
	/* [ ... ] -> [ ... prototype ] */

	JsValueRef object;
	JsValueRef prototype;

	object = get_value(object_index);
	JsGetPrototype(object, &prototype);
	throw_on_error();
	push_value(prototype, false);
}

const char*
jsal_get_string(int index)
{
	return jsal_get_lstring(index, NULL);
}

int
jsal_get_top(void)
{
	return vector_len(s_value_stack) - s_stack_base;
}

unsigned int
jsal_get_uint(int index)
{
	int        value;
	JsValueRef value_ref;

	value_ref = get_value(index);
	if (JsNumberToInt(value_ref, &value) != JsNoError)
		return 0;
	return (unsigned int)value;
}

bool
jsal_has_prop(int object_index)
{
	/* [ ... key ] -> [ ... ] */
	
	bool            has_property;
	JsPropertyIdRef key_ref;
	JsValueRef      object_ref;

	object_ref = get_value(object_index);
	key_ref = make_property_id(pop_value());
	JsHasProperty(object_ref, key_ref, &has_property);
	return has_property;
}

bool
jsal_has_prop_index(int object_index, int name)
{
	object_index = jsal_normalize_index(object_index);
	jsal_push_sprintf("%d", name);
	return jsal_has_prop(object_index);
}

bool
jsal_has_prop_string(int object_index, const char* name)
{
	object_index = jsal_normalize_index(object_index);
	jsal_push_string(name);
	return jsal_has_prop(object_index);
}

void
jsal_insert(int at_index)
{
	/* [ ... value ] -> [ ... value ... ] */
	
	js_ref_t ref;
	
	at_index = jsal_normalize_index(at_index);

	if (at_index == jsal_get_top() - 1)
		return;  // nop
	ref = *(js_ref_t*)vector_get(s_value_stack, vector_len(s_value_stack) - 1);
	vector_insert(s_value_stack, at_index + s_stack_base, &ref);
	vector_pop(s_value_stack, 1);
}

bool
jsal_is_array(int stack_index)
{
	JsValueRef  ref;
	JsValueType type;

	ref = get_value(stack_index);
	JsGetValueType(ref, &type);
	return type == JsArray;
}

bool
jsal_is_boolean(int stack_index)
{
	JsValueRef  ref;
	JsValueType type;

	ref = get_value(stack_index);
	JsGetValueType(ref, &type);
	return type == JsBoolean;
}

bool
jsal_is_buffer(int stack_index)
{
	JsValueRef  ref;
	JsValueType type;

	ref = get_value(stack_index);
	JsGetValueType(ref, &type);
	return type == JsArrayBuffer
		|| type == JsTypedArray;
}

bool
jsal_is_error(int stack_index)
{
	JsValueRef  ref;
	JsValueType type;

	ref = get_value(stack_index);
	JsGetValueType(ref, &type);
	return type == JsError;
}

bool
jsal_is_function(int stack_index)
{
	JsValueRef  ref;
	JsValueType type;

	ref = get_value(stack_index);
	JsGetValueType(ref, &type);
	return type == JsFunction;
}

bool
jsal_is_null(int stack_index)
{
	JsValueRef  ref;
	JsValueType type;

	ref = get_value(stack_index);
	JsGetValueType(ref, &type);
	return type == JsNull;
}

bool
jsal_is_number(int stack_index)
{
	JsValueRef  ref;
	JsValueType type;

	ref = get_value(stack_index);
	JsGetValueType(ref, &type);
	return type == JsNumber;
}

bool
jsal_is_object(int stack_index)
{
	JsValueRef  ref;
	JsValueType type;

	ref = get_value(stack_index);
	JsGetValueType(ref, &type);
	return type == JsObject
		|| type == JsArray
		|| type == JsArrayBuffer
		|| type == JsDataView
		|| type == JsError
		|| type == JsFunction
		|| type == JsTypedArray;
}

bool
jsal_is_object_coercible(int at_index)
{
	return !jsal_is_undefined(at_index)
		&& !jsal_is_null(at_index);
}

bool
jsal_is_string(int stack_index)
{
	JsValueRef  ref;
	JsValueType type;

	ref = get_value(stack_index);
	JsGetValueType(ref, &type);
	return type == JsString;
}

bool
jsal_is_symbol(int stack_index)
{
	JsValueRef  ref;
	JsValueType type;

	ref = get_value(stack_index);
	JsGetValueType(ref, &type);
	return type == JsSymbol;
}

bool
jsal_is_undefined(int stack_index)
{
	JsValueRef  ref;
	JsValueType type;

	ref = get_value(stack_index);
	JsGetValueType(ref, &type);
	return type == JsUndefined;
}

void
jsal_make_buffer(int object_index, js_buffer_type_t buffer_type, void* buffer, size_t num_items)
{
	JsValueRef       object;
	JsTypedArrayType type;

	object = get_value(object_index);
	type = buffer_type == JS_UINT8ARRAY ? JsArrayTypeUint8
		: buffer_type == JS_UINT8ARRAY_CLAMPED ? JsArrayTypeUint8Clamped
		: buffer_type == JS_UINT16ARRAY ? JsArrayTypeUint16
		: buffer_type == JS_UINT32ARRAY ? JsArrayTypeUint32
		: buffer_type == JS_INT8ARRAY ? JsArrayTypeInt8
		: buffer_type == JS_INT16ARRAY ? JsArrayTypeInt16
		: buffer_type == JS_INT32ARRAY ? JsArrayTypeInt32
		: buffer_type == JS_FLOAT32ARRAY ? JsArrayTypeFloat32
		: buffer_type == JS_FLOAT64ARRAY ? JsArrayTypeFloat64
		: JsArrayTypeUint8;
	JsSetIndexedPropertiesToExternalData(object, buffer, type, (unsigned int)num_items);
}

js_ref_t*
jsal_new_key(const char* name)
{
	JsPropertyIdRef key;

	JsCreatePropertyId(name, strlen(name), &key);
	return make_ref(key, false);
}

bool
jsal_next(int iter_index)
{
	bool finished;
	
	iter_index = jsal_normalize_index(iter_index);
	jsal_get_prop_string(iter_index, "next");
	jsal_dup(iter_index);
	jsal_call_method(0);
	jsal_get_prop_string(-1, "done");
	finished = jsal_to_boolean(-1);
	jsal_pop(1);
	if (!finished) {
		jsal_get_prop_string(-1, "value");
		jsal_remove(-2);
		return true;
	}
	else {
		jsal_pop(1);
		return false;
	}
}

int
jsal_normalize_index(int index)
{
	int real_index;
	int top;
	
	real_index = index;
	top = jsal_get_top();
	if (real_index < 0)
		real_index += top;
	if (real_index < 0 || real_index >= top)
		jsal_error(JS_REF_ERROR, "invalid stack index '%d'", index);
	return real_index;
}

void
jsal_parse(int at_index)
{
	at_index = jsal_normalize_index(at_index);
	jsal_push_eval("JSON.parse");
	jsal_push_eval("JSON");
	jsal_dup(at_index);
	jsal_call_method(1);
	jsal_replace(at_index);
}

void
jsal_pop(int num_values)
{
	int top;
	
	top = jsal_get_top();
	if (num_values > top)
		jsal_error(JS_RANGE_ERROR, "cannot pop %d values from value stack", num_values);
	jsal_set_top(top - num_values);
}

int
jsal_push_boolean(bool value)
{
	JsValueRef ref;

	JsBoolToBoolean(value, &ref);
	return push_value(ref, false);
}

int
jsal_push_callee(void)
{
	return push_value(s_callee_value, true);
}

int
jsal_push_constructor(js_function_t callback, const char* name, int min_args, int magic)
{
	JsValueRef       function;
	struct function* function_data;
	JsValueRef       name_string;

	function_data = calloc(1, sizeof(struct function));
	function_data->callback = callback;
	function_data->ctor_only = true;
	function_data->magic = magic;
	function_data->min_args = min_args;
	JsCreateString(name, strlen(name), &name_string);
	JsCreateNamedFunction(name_string, on_js_to_native_call, function_data, &function);
	return push_value(function, false);
}

int
jsal_push_eval(const char* source)
{
	JsValueRef name_string;
	JsValueRef source_string;
	JsValueRef value;

	JsCreateString(source, strlen(source), &source_string);
	JsCreateString("eval()", 6, &name_string);
	JsRun(source_string, s_next_source_context++, name_string, JsParseScriptAttributeLibraryCode, &value);
	throw_on_error();
	return push_value(value, false);
}

int
jsal_push_global_object(void)
{
	JsValueRef object;

	JsGetGlobalObject(&object);
	return push_value(object, true);
}

int
jsal_push_hidden_stash(void)
{
	return push_value(s_stash, true);
}

int
jsal_push_int(int value)
{
	JsValueRef ref;

	JsDoubleToNumber((double)value, &ref);
	return push_value(ref, false);
}

int
jsal_push_known_symbol(const char* name)
{
	jsal_push_global_object();
	jsal_get_prop_string(-1, "Symbol");
	jsal_get_prop_string(-1, name);
	jsal_remove(-2);
	jsal_remove(-2);
	return jsal_get_top() - 1;
}

int
jsal_push_lstring(const char* value, size_t length)
{
	JsValueRef ref;

	JsCreateString(value, length, &ref);
	return push_value(ref, false);
}

int
jsal_push_new_array(void)
{
	JsValueRef ref;

	JsCreateArray(0, &ref);
	return push_value(ref, false);
}

int
jsal_push_new_bare_object(void)
{
	int index;

	index = jsal_push_new_object();
	jsal_push_null();
	jsal_set_prototype(-2);
	return index;
}

int
jsal_push_new_buffer(js_buffer_type_t type, size_t size)
{
	JsValueRef  buffer;
	JsErrorCode result;

	result = type == JS_INT8ARRAY ? JsCreateTypedArray(JsArrayTypeInt8, JS_INVALID_REFERENCE, 0, (unsigned int)size, &buffer)
		: type == JS_INT16ARRAY ? JsCreateTypedArray(JsArrayTypeInt16, JS_INVALID_REFERENCE, 0, (unsigned int)size, &buffer)
		: type == JS_INT32ARRAY ? JsCreateTypedArray(JsArrayTypeInt32, JS_INVALID_REFERENCE, 0, (unsigned int)size, &buffer)
		: type == JS_UINT8ARRAY ? JsCreateTypedArray(JsArrayTypeUint8, JS_INVALID_REFERENCE, 0, (unsigned int)size, &buffer)
		: type == JS_UINT8ARRAY_CLAMPED ? JsCreateTypedArray(JsArrayTypeUint8Clamped, JS_INVALID_REFERENCE, 0, (unsigned int)size, &buffer)
		: type == JS_UINT16ARRAY ? JsCreateTypedArray(JsArrayTypeUint16, JS_INVALID_REFERENCE, 0, (unsigned int)size, &buffer)
		: type == JS_UINT32ARRAY ? JsCreateTypedArray(JsArrayTypeUint32, JS_INVALID_REFERENCE, 0, (unsigned int)size, &buffer)
		: type == JS_FLOAT32ARRAY ? JsCreateTypedArray(JsArrayTypeFloat32, JS_INVALID_REFERENCE, 0, (unsigned int)size, &buffer)
		: type == JS_FLOAT64ARRAY ? JsCreateTypedArray(JsArrayTypeFloat64, JS_INVALID_REFERENCE, 0, (unsigned int)size, &buffer)
		: JsCreateArrayBuffer((unsigned int)size, &buffer);
	return push_value(buffer, false);
}

int
jsal_push_new_error(js_error_type_t type, const char* format, ...)
{
	va_list ap;
	int     index;

	va_start(ap, format);
	index = jsal_push_new_error_va(type, format, ap);
	va_end(ap);
	return index;
}

int
jsal_push_new_error_va(js_error_type_t type, const char* format, va_list ap)
{
	char*       message;
	JsValueRef  message_ref;
	JsValueRef  ref;
	JsErrorCode result;

	vasprintf(&message, format, ap);
	JsCreateString(message, strlen(message), &message_ref);
	result = type == JS_RANGE_ERROR ? JsCreateRangeError(message_ref, &ref)
		: type == JS_REF_ERROR ? JsCreateReferenceError(message_ref, &ref)
		: type == JS_SYNTAX_ERROR ? JsCreateSyntaxError(message_ref, &ref)
		: type == JS_TYPE_ERROR ? JsCreateTypeError(message_ref, &ref)
		: type == JS_URI_ERROR ? JsCreateURIError(message_ref, &ref)
		: JsCreateError(message_ref, &ref);
	return push_value(ref, false);
}

int
jsal_push_new_function(js_function_t callback, const char* name, int min_args, int magic)
{
	JsValueRef       function;
	struct function* function_data;
	JsValueRef       name_string;

	function_data = calloc(1, sizeof(struct function));
	function_data->callback = callback;
	function_data->magic = magic;
	function_data->min_args = min_args;
	JsCreateString(name, strlen(name), &name_string);
	JsCreateNamedFunction(name_string, on_js_to_native_call, function_data, &function);
	return push_value(function, false);
}

int
jsal_push_new_host_object(void* data, js_finalizer_t finalizer)
{
	JsValueRef     object;
	struct object* object_info;

	object_info = calloc(1, sizeof(struct object));
	JsCreateExternalObject(object_info, on_finalize_host_object, &object);

	object_info->data = data;
	object_info->finalizer = finalizer;
	object_info->object = object;
	
	return push_value(object, false);
}

int
jsal_push_new_iterator(int for_index)
{
	JsValueRef key_list;
	JsValueRef object;
	
	for_index = jsal_normalize_index(for_index);
	jsal_push_known_symbol("iterator");
	if (jsal_get_prop(for_index)) {
		jsal_dup(for_index);
		jsal_call_method(0);
	}
	else {
		jsal_pop(1);
		object = get_value(for_index);
		JsGetOwnPropertyNames(object, &key_list);
		push_value(key_list, true);
		jsal_push_new_iterator(-1);
		jsal_remove(-2);
	}
	return jsal_get_top() - 1;
}

int
jsal_push_new_object(void)
{
	JsValueRef ref;

	JsCreateObject(&ref);
	return push_value(ref, false);
}

int
jsal_push_new_promise(js_ref_t* *out_resolver, js_ref_t* *out_rejector)
{
	JsValueRef promise;
	JsValueRef rejector;
	JsValueRef resolver;

	JsCreatePromise(&promise, &resolver, &rejector);
	*out_resolver = make_ref(resolver, false);
	if (out_rejector != NULL)
		*out_rejector = make_ref(rejector, false);
	return push_value(promise, false);
}

int
jsal_push_new_symbol(const char* description)
{
	JsValueRef name_ref;
	JsValueRef ref;

	JsCreateString(description, strlen(description), &name_ref);
	JsCreateSymbol(name_ref, &ref);
	return push_value(ref, false);
}

int
jsal_push_null(void)
{
	JsValueRef ref;

	JsGetNullValue(&ref);
	return push_value(ref, true);
}

int
jsal_push_number(double value)
{
	JsValueRef ref;
	
	JsDoubleToNumber(value, &ref);
	return push_value(ref, false);
}

int
jsal_push_ref(js_ref_t* ref)
{
	return push_value(ref->value, false);
}

int
jsal_push_sprintf(const char* format, ...)
{
	va_list ap;
	int     index;
	char*   string;

	va_start(ap, format);
	vasprintf(&string, format, ap);
	va_end(ap);
	index = jsal_push_string(string);
	free(string);
	return index;
}

int
jsal_push_string(const char* value)
{
	JsValueRef string_value;

	JsCreateString(value, strlen(value), &string_value);
	return push_value(string_value, false);
}

int
jsal_push_this(void)
{
	if (s_this_value == JS_INVALID_REFERENCE)
		jsal_error(JS_REF_ERROR, "no known 'this' binding");
	
	// it's safe for this to be a weak reference: `this` can't be garbage collected
	// while the function using it runs and anything pushed onto the value stack
	// is unwound on return, so the stack entry can't persist beyond that point by
	// definition.
	return push_value(s_this_value, true);
}

int
jsal_push_uint(unsigned int value)
{
	JsValueRef ref;

	JsDoubleToNumber((double)value, &ref);
	return push_value(ref, false);
}

int
jsal_push_undefined(void)
{
	JsValueRef ref;

	JsGetUndefinedValue(&ref);
	return push_value(ref, true);
}

void
jsal_pull(int from_index)
{
	js_ref_t ref;

	from_index = jsal_normalize_index(from_index);
	ref = *(js_ref_t*)vector_get(s_value_stack, from_index + s_stack_base);
	vector_push(s_value_stack, &ref);
	vector_remove(s_value_stack, from_index + s_stack_base);
}

void
jsal_put_prop(int object_index)
{
	/* [ ... key value ] -> [ ... ] */

	JsPropertyIdRef key;
	JsValueRef      key_value;
	JsValueRef      object;
	JsValueRef      value;

	object = get_value(object_index);
	value = pop_value();
	if (jsal_is_number(-1)) {
		key_value = pop_value();
		JsSetIndexedProperty(object, key_value, value);
	}
	else {
		key = make_property_id(pop_value());
		JsSetProperty(object, key, value, true);
	}
	throw_on_error();
}

void
jsal_put_prop_index(int object_index, int name)
{
	/* [ ... value ] -> [ ... ] */

	JsValueRef index;
	JsValueRef object;
	JsValueRef value;

	object = get_value(object_index);
	value = pop_value();
	JsIntToNumber(name, &index);
	JsSetIndexedProperty(object, index, value);
	throw_on_error();
}

void
jsal_put_prop_key(int object_index, js_ref_t* key)
{
	/* [ ... value ] -> [ ... ] */

	JsValueRef      object;
	JsValueRef      value;

	object = get_value(object_index);
	value = pop_value();
	JsSetProperty(object, key->value, value, true);
	throw_on_error();
}

void
jsal_put_prop_string(int object_index, const char* name)
{
	/* [ ... value ] -> [ ... ] */

	JsPropertyIdRef key;
	JsValueRef      object;
	JsValueRef      value;

	object = get_value(object_index);
	value = pop_value();
	JsCreatePropertyId(name, strlen(name), &key);
	JsSetProperty(object, key, value, true);
	throw_on_error();
}

js_ref_t*
jsal_ref(int at_index)
{
	js_ref_t*  ref;
	JsValueRef value;

	value = get_value(at_index);
	JsAddRef(value, NULL);

	ref = calloc(1, sizeof(js_ref_t));
	ref->value = value;
	return ref;
}

void
jsal_remove(int at_index)
{
	/* [ ... value ... ] -> [ ... ] */

	js_ref_t* ref;

	at_index = jsal_normalize_index(at_index);
	ref = vector_get(s_value_stack, at_index + s_stack_base);
	if (!ref->weak_ref)
		JsRelease(ref->value, NULL);
	vector_remove(s_value_stack, at_index + s_stack_base);
}

bool
jsal_replace(int at_index)
{
	/* [ ... old_value ... new_value ] -> [ ... new_value ... ] */

	js_ref_t* old_ref;
	js_ref_t* ref;

	at_index = jsal_normalize_index(at_index);

	if (at_index == jsal_get_top() - 1)
		return true;  // nop
	old_ref = vector_get(s_value_stack, at_index + s_stack_base);
	ref = vector_get(s_value_stack, vector_len(s_value_stack) - 1);
	if (!old_ref->weak_ref)
		JsRelease(old_ref->value, NULL);
	vector_put(s_value_stack, at_index + s_stack_base, ref);
	vector_pop(s_value_stack, 1);
	return true;
}

void
jsal_require_array(int at_index)
{
	if (!jsal_is_array(at_index)) {
		jsal_dup(at_index);
		jsal_push_new_error(JS_TYPE_ERROR, "'%s' is not an array", jsal_to_string(-1));
		jsal_remove(-2);
		jsal_throw();
	}
}

bool
jsal_require_boolean(int at_index)
{
	if (!jsal_is_boolean(at_index)) {
		jsal_dup(at_index);
		jsal_push_new_error(JS_TYPE_ERROR, "'%s' is not a boolean", jsal_to_string(-1));
		jsal_remove(-2);
		jsal_throw();
	}
	return jsal_get_boolean(at_index);
}

void*
jsal_require_buffer_ptr(int at_index, size_t *out_size)
{
	if (!jsal_is_buffer(at_index)) {
		jsal_dup(at_index);
		jsal_push_new_error(JS_TYPE_ERROR, "'%s' is not a buffer", jsal_to_string(-1));
		jsal_remove(-2);
		jsal_throw();
	}
	return jsal_get_buffer_ptr(at_index, out_size);
}

void
jsal_require_function(int at_index)
{
	if (!jsal_is_function(at_index)) {
		jsal_dup(at_index);
		jsal_push_new_error(JS_TYPE_ERROR, "'%s' is not a function", jsal_to_string(-1));
		jsal_remove(-2);
		jsal_throw();
	}
}

int
jsal_require_int(int at_index)
{
	jsal_require_number(at_index);
	return jsal_get_int(at_index);
}

const char*
jsal_require_lstring(int at_index, size_t *out_length)
{
	if (!jsal_is_string(at_index)) {
		jsal_dup(at_index);
		jsal_push_new_error(JS_TYPE_ERROR, "'%s' is not a string", jsal_to_string(-1));
		jsal_remove(-2);
		jsal_throw();
	}
	return jsal_get_lstring(at_index, out_length);
}

void
jsal_require_null(int at_index)
{
	if (!jsal_is_null(at_index)) {
		jsal_dup(at_index);
		jsal_push_new_error(JS_TYPE_ERROR, "'%s' is not 'null'", jsal_to_string(-1));
		jsal_remove(-2);
		jsal_throw();
	}
}

double
jsal_require_number(int at_index)
{
	if (!jsal_is_number(at_index)) {
		jsal_dup(at_index);
		jsal_push_new_error(JS_TYPE_ERROR, "'%s' is not a number", jsal_to_string(-1));
		jsal_remove(-2);
		jsal_throw();
	}
	return jsal_get_number(at_index);
}

void
jsal_require_object(int at_index)
{
	if (!jsal_is_object(at_index)) {
		jsal_dup(at_index);
		jsal_push_new_error(JS_TYPE_ERROR, "'%s' is not an object", jsal_to_string(-1));
		jsal_remove(-2);
		jsal_throw();
	}
}

void
jsal_require_object_coercible(int at_index)
{
	if (!jsal_is_object_coercible(at_index)) {
		jsal_dup(at_index);
		jsal_push_new_error(JS_TYPE_ERROR, "'%s' is not object-coercible", jsal_to_string(-1));
		jsal_remove(-2);
		jsal_throw();
	}
}

const char*
jsal_require_string(int at_index)
{
	return jsal_require_lstring(at_index, NULL);
}

void
jsal_require_symbol(int at_index)
{
	if (!jsal_is_symbol(at_index)) {
		jsal_dup(at_index);
		jsal_push_new_error(JS_TYPE_ERROR, "'%s' is not a symbol", jsal_to_string(-1));
		jsal_remove(-2);
		jsal_throw();
	}
}

unsigned int
jsal_require_uint(int at_index)
{
	jsal_require_number(at_index);
	return jsal_get_uint(at_index);
}

void
jsal_require_undefined(int at_index)
{
	if (!jsal_is_null(at_index)) {
		jsal_dup(at_index);
		jsal_push_new_error(JS_TYPE_ERROR, "'%s' is not 'undefined'", jsal_to_string(-1));
		jsal_remove(-2);
		jsal_throw();
	}
}

void
jsal_set_finalizer(int at_index, js_finalizer_t callback)
{
	JsValueRef     object;
	struct object* object_info;
	void*          ptr;

	object = get_value(at_index);
	if (JsGetExternalData(object, &ptr) != JsNoError)
		return;
	object_info = ptr;
	object_info->finalizer = callback;
}

void
jsal_set_host_data(int at_index, void* ptr)
{
	void*          data;
	JsValueRef     object;
	struct object* object_info;

	object = get_value(at_index);
	if (JsGetExternalData(object, &data) != JsNoError)
		return;
	object_info = data;
	object_info->data = ptr;
}

void
jsal_set_prototype(int object_index)
{
	/* [ ... prototype ] -> [ ... ] */

	JsValueRef object;
	JsValueRef prototype;

	object = get_value(object_index);
	prototype = pop_value();
	JsSetPrototype(object, prototype);
	throw_on_error();
}

void
jsal_set_top(int new_top)
{
	int new_size;

	new_size = new_top + s_stack_base;
	resize_stack(new_size);
}

void
jsal_stringify(int at_index)
{
	at_index = jsal_normalize_index(at_index);
	jsal_push_eval("JSON.stringify");
	jsal_push_eval("JSON");
	jsal_dup(at_index);
	jsal_call_method(1);
	jsal_replace(at_index);
}

void
jsal_throw(void)
{
	/* [ ... exception ] -> [ ... ] */

	JsValueRef ref;

	ref = pop_value();
	throw_value(ref);
}

bool
jsal_to_boolean(int at_index)
{
	JsValueRef value;

	at_index = jsal_normalize_index(at_index);
	value = get_value(at_index);
	JsConvertValueToBoolean(value, &value);
	throw_on_error();
	push_value(value, false);
	jsal_replace(at_index);
	return jsal_get_boolean(at_index);
}

int
jsal_to_int(int at_index)
{
	jsal_to_number(at_index);
	return jsal_get_int(at_index);
}

double
jsal_to_number(int at_index)
{
	JsValueRef value;

	at_index = jsal_normalize_index(at_index);
	value = get_value(at_index);
	JsConvertValueToNumber(value, &value);
	throw_on_error();
	push_value(value, false);
	jsal_replace(at_index);
	return jsal_get_number(at_index);
}

void
jsal_to_object(int at_index)
{
	JsValueRef value;

	at_index = jsal_normalize_index(at_index);
	value = get_value(at_index);
	JsConvertValueToObject(value, &value);
	throw_on_error();
	push_value(value, false);
	jsal_replace(at_index);
}

const char*
jsal_to_string(int at_index)
{
	JsValueRef value;

	at_index = jsal_normalize_index(at_index);
	value = get_value(at_index);
	JsConvertValueToString(value, &value);
	throw_on_error();
	push_value(value, false);
	jsal_replace(at_index);
	return jsal_get_string(at_index);
}

bool
jsal_try(js_function_t callback, int num_args)
{
	/* [ ... arg1..argN ] -> [ ... retval ] */

	jsal_jmpbuf label;
	int         last_stack_base;
	JsValueRef  result;
	bool        retval = true;

	last_stack_base = s_stack_base;
	s_stack_base = vector_len(s_value_stack) - num_args;
	if (jsal_setjmp(label) == 0) {
		vector_push(s_catch_stack, label);
		if (!callback(num_args, false, 0))
			jsal_push_undefined();
		result = pop_value();
		vector_pop(s_catch_stack, 1);
	}
	else {
		result = pop_value();
		retval = false;
	}
	resize_stack(s_stack_base);
	s_stack_base = last_stack_base;
	push_value(result, false);
	return retval;
}

bool
jsal_try_call(int num_args)
{
	/* [ ... function arg1..argN ] -> [ ... retval ] */

	jsal_jmpbuf label;

	if (jsal_setjmp(label) == 0) {
		vector_push(s_catch_stack, label);
		jsal_call(num_args);
		vector_pop(s_catch_stack, 1);
		return true;
	}
	else {
		return false;
	}
}

bool
jsal_try_call_method(int num_args)
{
	/* [ ... function this arg1..argN ] -> [ ... retval ] */

	jsal_jmpbuf label;

	if (jsal_setjmp(label) == 0) {
		vector_push(s_catch_stack, label);
		jsal_call_method(num_args);
		vector_pop(s_catch_stack, 1);
		return true;
	}
	else {
		return false;
	}
}

bool
jsal_try_compile(const char* filename)
{
	/* [ ... source ] -> [ ... function ] */

	jsal_jmpbuf label;

	if (jsal_setjmp(label) == 0) {
		vector_push(s_catch_stack, label);
		jsal_compile(filename);
		vector_pop(s_catch_stack, 1);
		return true;
	}
	else {
		return false;
	}
}

bool
jsal_try_construct(int num_args)
{
	/* [ ... constructor arg1..argN ] -> [ ... retval ] */

	jsal_jmpbuf label;

	if (jsal_setjmp(label) == 0) {
		vector_push(s_catch_stack, label);
		jsal_construct(num_args);
		vector_pop(s_catch_stack, 1);
		return true;
	}
	else {
		return false;
	}
}

bool
jsal_try_eval_module(const char* filename)
{
	/* [ ... source ] -> [ ... exports ] */

	jsal_jmpbuf label;

	if (jsal_setjmp(label) == 0) {
		vector_push(s_catch_stack, label);
		jsal_eval_module(filename);
		vector_pop(s_catch_stack, 1);
		return true;
	}
	else {
		return false;
	}
}

bool
jsal_try_parse(int at_index)
{
	jsal_jmpbuf label;

	if (jsal_setjmp(label) == 0) {
		vector_push(s_catch_stack, label);
		jsal_parse(at_index);
		vector_pop(s_catch_stack, 1);
		return true;
	}
	else {
		return false;
	}
}

void
jsal_unref(js_ref_t* ref)
{
	JsRelease(ref->value, NULL);
	free(ref);
}

bool
jsal_debug_init(js_break_callback_t on_breakpoint)
{
	s_break_callback = on_breakpoint;
	
	if (JsDiagStartDebugging(s_js_runtime, on_debugger_event, NULL) != JsNoError)
		return false;
	JsDiagSetBreakOnException(s_js_runtime, JsDiagBreakOnExceptionAttributeUncaught);
	return true;
}

void
jsal_debug_uninit(void)
{
	void* userdata;

	JsDiagStopDebugging(s_js_runtime, &userdata);
}

void
jsal_debug_on_throw(js_throw_callback_t callback)
{
	s_throw_callback = callback;
}

int
jsal_debug_breakpoint_add(const char* filename, unsigned int line, unsigned int column)
{
	struct breakpoint breakpoint;
	JsValueRef        result;
	unsigned int      script_id;

	breakpoint.filename = strdup(filename);
	breakpoint.line = line;
	breakpoint.column = column;
	breakpoint.id = 0;
	script_id = script_id_from_filename(filename);
	if (script_id < UINT_MAX) {
		if (JsDiagSetBreakpoint(script_id, line - 1, column - 1, &result) != JsNoError)
			goto finished;
		push_value(result, true);
		jsal_get_prop_string(-1, "breakpointId");
		breakpoint.id = jsal_get_uint(-1);
		jsal_pop(2);
	}

finished:
	vector_push(s_breakpoints, &breakpoint);
	return vector_len(s_breakpoints) - 1;
}

void
jsal_debug_breakpoint_inject(void)
{
	JsDiagRequestAsyncBreak(s_js_runtime);
}

void
jsal_debug_breakpoint_remove(int index)
{
	struct breakpoint* breakpoint;
	unsigned int       id;
	JsValueRef         list;
	
	breakpoint = vector_get(s_breakpoints, index);
	
	JsDiagGetBreakpoints(&list);
	push_value(list, true);
	jsal_push_new_iterator(-1);
	while (jsal_next(-1)) {
		jsal_get_prop_string(-1, "breakpointId");
		id = jsal_get_uint(-1);
		if (breakpoint->id == id)
			JsDiagRemoveBreakpoint(id);
		jsal_pop(2);
	}
	jsal_pop(2);
	vector_remove(s_breakpoints, index);
}

bool
jsal_debug_inspect_call(int call_index)
{
	/* [ ... ] -> [ ... filename function_name line column ] */
	
	JsValueRef   backtrace;
	unsigned int handle;
	JsValueRef   function_data;

	if (JsDiagGetStackTrace(&backtrace) != JsNoError)
		return false;
	push_value(backtrace, true);
	if (jsal_get_prop_index(-1, call_index)) {
		jsal_get_prop_string(-1, "scriptId");
		jsal_push_string(filename_from_script_id(jsal_get_uint(-1)));
		jsal_replace(-2);

		jsal_get_prop_string(-2, "functionHandle");
		handle = jsal_get_uint(-1);
		JsDiagGetObjectFromHandle(handle, &function_data);
		push_value(function_data, true);
		if (!jsal_get_prop_string(-1, "name")) {
			jsal_pop(1);
			jsal_push_string("");
		}
		jsal_remove(-2);
		jsal_remove(-2);

		jsal_get_prop_string(-3, "line");
		jsal_get_prop_string(-4, "column");
		jsal_remove(-5);
		jsal_remove(-5);
		return true;
	}
	else {
		jsal_pop(2);
		return false;
	}
}

bool
jsal_debug_inspect_breakpoint(int index)
{
	/* [ ... ] -> [ ... filename line column ] */

	struct breakpoint* breakpoint;

	if (index >= vector_len(s_breakpoints))
		return false;
	breakpoint = vector_get(s_breakpoints, index);
	jsal_push_string(breakpoint->filename);
	jsal_push_int(breakpoint->line);
	jsal_push_int(breakpoint->column);
	return true;
}

bool
jsal_debug_inspect_eval(int call_index, const char* source, bool *out_errored)
{
	/* [ ... ] -> [ type value_summary handle ] */

	JsErrorCode error_code;
	bool        is_object;
	JsValueRef  result;
	JsValueRef  source_string;
	const char* type;

	JsCreateString(source, strlen(source), &source_string);
	error_code = JsDiagEvaluate(source_string, call_index, JsParseScriptAttributeNone, false, &result);
	if (error_code != JsNoError && error_code != JsErrorScriptException)
		return false;
	*out_errored = error_code != JsNoError;
	push_value(result, true);
	if (jsal_has_prop_string(-1, "type"))
		jsal_get_prop_string(-1, "type");
	else
		jsal_push_string("unknown");
	type = jsal_get_string(-1);
	is_object = strcmp(type, "object") == 0 || strcmp(type, "function") == 0;
	if (jsal_has_prop_string(-2, "display"))
		jsal_get_prop_string(-2, "display");
	else
		jsal_get_prop_string(-2, "value");
	if (is_object)
		jsal_get_prop_string(-3, "handle");
	else
		jsal_push_null();
	jsal_remove(-4);
	return true;
}

bool
jsal_debug_inspect_object(unsigned int handle, int property_index)
{
	/* [ ... ] -> [ ... key value handle ] */

	bool        is_object = false;
	JsValueRef  results;
	const char* type;

	if (JsDiagGetProperties(handle, property_index, 1, &results) != JsNoError)
		return false;
	push_value(results, true);
	jsal_get_prop_string(-1, "properties");
	if (!jsal_get_prop_index(-1, 0)) {
		jsal_pop(3);
		return false;
	}
	jsal_get_prop_string(-1, "type");
	type = jsal_get_string(-1);
	if (type != NULL)
		is_object = strcmp(type, "object") == 0 || strcmp(type, "function") == 0;
	jsal_pop(1);
	jsal_get_prop_string(-1, "name");
	if (jsal_has_prop_string(-2, "display"))
		jsal_get_prop_string(-2, "display");
	else
		jsal_get_prop_string(-2, "value");
	if (is_object)
		jsal_get_prop_string(-3, "handle");
	else
		jsal_push_null();
	jsal_remove(-4);
	jsal_remove(-4);
	jsal_remove(-4);
	return true;
}

bool
jsal_debug_inspect_var(int call_index, int var_index)
{
	/* [ ... ] -> [ ... name type value_summary handle ] */

	JsValueRef  frame_info;
	bool        is_object = false;
	const char* type;

	if (JsDiagGetStackProperties(call_index, &frame_info) != JsNoError)
		return false;
	push_value(frame_info, true);
	jsal_get_prop_string(-1, "locals");
	if (jsal_get_prop_index(-1, var_index)) {
		jsal_get_prop_string(-1, "type");
		type = jsal_get_string(-1);
		if (type != NULL)
			is_object = strcmp(type, "object") == 0 || strcmp(type, "function") == 0;
		jsal_pop(1);
		jsal_get_prop_string(-1, "name");
		if (jsal_has_prop_string(-2, "type"))
			jsal_get_prop_string(-2, "type");
		else
			jsal_push_string("unknown");
		if (jsal_has_prop_string(-3, "display"))
			jsal_get_prop_string(-3, "display");
		else
			jsal_get_prop_string(-3, "value");
		if (is_object)
			jsal_get_prop_string(-4, "handle");
		else
			jsal_push_null();
		jsal_remove(-5);
		jsal_remove(-5);
		jsal_remove(-5);
		return true;
	}
	else {
		jsal_pop(3);
		return false;
	}
}

static void
free_ref(js_ref_t* ref)
{
	if (!ref->weak_ref)
		JsRelease(ref->value, NULL);
	free(ref);
}

static JsValueRef
get_value(int stack_index)
{
	js_ref_t* ref;

	stack_index = jsal_normalize_index(stack_index);
	ref = vector_get(s_value_stack, stack_index + s_stack_base);
	return ref->value;
}

static JsPropertyIdRef
make_property_id(JsValueRef key)
{
	char*           key_name;
	size_t          key_length;
	JsValueType     key_type;
	JsPropertyIdRef property_id;

	JsGetValueType(key, &key_type);
	if (key_type == JsSymbol) {
		JsGetPropertyIdFromSymbol(key, &property_id);
	}
	else {
		JsCopyString(key, NULL, 0, &key_length);
		key_name = malloc(key_length);
		JsCopyString(key, key_name, key_length, NULL);
		JsCreatePropertyId(key_name, key_length, &property_id);
		free(key_name);
	}
	return property_id;
}

js_ref_t*
make_ref(JsRef value, bool weak_ref)
{
	js_ref_t* ref;
	
	if (!weak_ref)
		JsAddRef(value, NULL);

	ref = calloc(1, sizeof(js_ref_t));
	ref->value = value;
	ref->weak_ref = weak_ref;
	return ref;
}

static JsValueRef
pop_value(void)
{
	int        index;
	js_ref_t*  ref;
	JsValueRef value;

	index = vector_len(s_value_stack) - 1;
	ref = vector_get(s_value_stack, index);
	value = ref->value;
	if (!ref->weak_ref)
		JsRelease(ref->value, NULL);
	vector_pop(s_value_stack, 1);
	return value;
}

static void
push_debug_callback_args(JsValueRef event_data)
{
	push_value(event_data, true);
	jsal_get_prop_string(-1, "scriptId");
	jsal_push_string(filename_from_script_id(jsal_get_uint(-1)));
	jsal_replace(-2);
	jsal_get_prop_string(-2, "line");
	jsal_get_prop_string(-3, "column");
	jsal_remove(-4);
}

static unsigned int
script_id_from_filename(const char* filename)
{
	bool         have_name;
	unsigned int script_id = UINT_MAX;
	JsValueRef   script_list;

	JsDiagGetScripts(&script_list);
	push_value(script_list, true);
	jsal_push_new_iterator(-1);
	while (jsal_next(-1)) {
		have_name = jsal_get_prop_string(-1, "fileName");
		if (have_name && strcmp(filename, jsal_get_string(-1)) == 0) {
			jsal_get_prop_string(-2, "scriptId");
			script_id = jsal_get_uint(-1);
			jsal_pop(3);
			break;
		}
		else {
			jsal_pop(2);
		}
	}
	jsal_pop(2);
	return script_id;
}

static const char*
filename_from_script_id(unsigned int script_id)
{
	const char* filename = NULL;
	JsValueRef  script_list;
	
	JsDiagGetScripts(&script_list);
	push_value(script_list, true);
	jsal_push_new_iterator(-1);
	while (jsal_next(-1)) {
		jsal_get_prop_string(-1, "scriptId");
		if (script_id == jsal_get_uint(-1)) {
			jsal_get_prop_string(-2, "fileName");
			filename = jsal_get_string(-1);
			jsal_pop(3);
			break;
		}
		else {
			jsal_pop(2);
		}
	}
	jsal_pop(2);
	return filename;
}

static JsModuleRecord
get_module_record(const char* filename, JsModuleRecord parent, bool *out_is_new)
{
	struct module* cached;
	struct module  module;
	JsModuleRecord module_record;
	JsValueRef     specifier;
	
	iter_t iter;

	*out_is_new = false;
	iter = vector_enum(s_module_cache);
	while (cached = iter_next(&iter)) {
		if (strcmp(filename, cached->filename) == 0)
			return cached->record;
	}
	
	*out_is_new = true;
	JsCreateString(filename, strlen(filename), &specifier);
	JsInitializeModuleRecord(parent, specifier, &module_record);
	JsSetModuleHostInfo(module_record, JsModuleHostInfo_FetchImportedModuleCallback, on_fetch_imported_module);
	JsSetModuleHostInfo(module_record, JsModuleHostInfo_FetchImportedModuleFromScriptCallback, on_fetch_dynamic_import);
	JsSetModuleHostInfo(module_record, JsModuleHostInfo_NotifyModuleReadyCallback, on_notify_module_ready);
	JsSetModuleHostInfo(module_record, JsModuleHostInfo_HostDefined, specifier);
	JsAddRef(module_record, NULL);
	module.filename = strdup(filename);
	module.record = module_record;
	vector_push(s_module_cache, &module);
	return module_record;
}

static int
push_value(JsValueRef value, bool weak_ref)
{
	js_ref_t ref;
	
	if (!weak_ref)
		JsAddRef(value, NULL);
	ref.value = value;
	ref.weak_ref = weak_ref;
	vector_push(s_value_stack, &ref);
	return vector_len(s_value_stack) - s_stack_base - 1;
}

static void
resize_stack(int new_size)
{
	js_ref_t   ref;
	js_ref_t*  ref_ptr;
	int        old_size;
	JsValueRef undefined;

	int i;

	old_size = vector_len(s_value_stack);
	if (new_size < old_size) {
		for (i = new_size; i < old_size; ++i) {
			ref_ptr = vector_get(s_value_stack, i);
			if (!ref_ptr->weak_ref)
				JsRelease(ref_ptr->value, NULL);
		}
	}
	vector_resize(s_value_stack, new_size);
	if (new_size > old_size) {
		JsGetUndefinedValue(&undefined);
		ref.value = undefined;
		ref.weak_ref = true;
		for (i = old_size; i < new_size; ++i)
			vector_put(s_value_stack, i, &ref);
	}
}

static void
throw_on_error(void)
{
	JsValueRef  error_ref;
	bool        has_exception;
	JsErrorCode result;

	JsHasException(&has_exception);
	if (has_exception) {
		result = JsGetAndClearException(&error_ref);
		throw_value(error_ref);
	}
}

static void
throw_value(JsValueRef value)
{
	int         index;
	jsal_jmpbuf label;

	push_value(value, false);
	index = vector_len(s_catch_stack) - 1;
	if (index >= 0) {
		memcpy(label, vector_get(s_catch_stack, index), sizeof(jsal_jmpbuf));
		vector_pop(s_catch_stack, 1);
		jsal_longjmp(label, 1);
	}
	else {
		printf("JS exception thrown from unguarded C code!\n");
		printf("-> %s\n", jsal_to_string(-1));
		abort();
	}
}

static void CHAKRA_CALLBACK
on_debugger_event(JsDiagDebugEvent event_type, JsValueRef data, void* userdata)
{
	struct breakpoint* breakpoint;
	JsValueRef         breakpoint_info;
	const char*        filename;
	unsigned int       handle;
	jsal_jmpbuf        label;
	int                last_stack_base;
	const char*        name;
	JsValueRef         properties;
	unsigned int       script_id;
	js_step_t          step = JS_STEP_CONTINUE;
	JsDiagStepType     step_type;
	char*              traceback;

	iter_t iter;

	switch (event_type) {
		case JsDiagDebugEventSourceCompile:
			push_value(data, true);
			jsal_get_prop_string(-1, "scriptId");
			jsal_get_prop_string(-2, "fileName");
			script_id = jsal_get_uint(-2);
			filename = jsal_get_string(-1);
			jsal_pop(3);
			iter = vector_enum(s_breakpoints);
			while (iter_next(&iter)) {
				breakpoint = iter.ptr;
				if (strcmp(filename, breakpoint->filename) == 0) {
					if (JsDiagSetBreakpoint(script_id, breakpoint->line - 1, breakpoint->column - 1, &breakpoint_info) != JsNoError)
						continue;
					push_value(breakpoint_info, true);
					jsal_get_prop_string(-1, "breakpointId");
					breakpoint->id = jsal_get_uint(-1);
					jsal_pop(2);
				}
			}
			break;
		case JsDiagDebugEventRuntimeException:
			last_stack_base = s_stack_base;
			s_stack_base = vector_len(s_value_stack);
			push_value(data, true);
			jsal_get_prop_string(-1, "exception");
			jsal_get_prop_string(-1, "handle");
			handle = jsal_get_uint(-1);
			jsal_pop(3);
			JsDiagGetProperties(handle, 0, UINT_MAX, &properties);
			push_value(properties, true);
			jsal_get_prop_string(-1, "properties");
			jsal_push_new_iterator(-1);
			while (jsal_next(-1)) {
				jsal_get_prop_string(-1, "name");
				name = jsal_get_string(-1);
				if (name != NULL && strcmp(name, "stack") == 0) {
					jsal_get_prop_string(-2, "value");
					traceback = strdup(jsal_get_string(-1));
					jsal_pop(3);
					break;
				}
				jsal_pop(2);
			}
			jsal_pop(3);
			jsal_push_string(traceback);
			push_debug_callback_args(data);
			if (jsal_setjmp(label) == 0) {
				vector_push(s_catch_stack, label);
				if (s_throw_callback != NULL)
					s_throw_callback();
				vector_pop(s_catch_stack, 1);
			}
			else {
				jsal_pop(1);
			}
			resize_stack(s_stack_base);
			s_stack_base = last_stack_base;
			// fallthrough;
		case JsDiagDebugEventAsyncBreak:
		case JsDiagDebugEventBreakpoint:
		case JsDiagDebugEventDebuggerStatement:
		case JsDiagDebugEventStepComplete:
			last_stack_base = s_stack_base;
			s_stack_base = vector_len(s_value_stack);
			push_debug_callback_args(data);
			if (jsal_setjmp(label) == 0) {
				vector_push(s_catch_stack, label);
				step = s_break_callback();
				vector_pop(s_catch_stack, 1);
			}
			else {
				jsal_pop(1);
			}
			resize_stack(s_stack_base);
			s_stack_base = last_stack_base;
			step_type = step == JS_STEP_IN ? JsDiagStepTypeStepIn
				: step == JS_STEP_OUT ? JsDiagStepTypeStepOut
				: step == JS_STEP_OVER ? JsDiagStepTypeStepOver
				: JsDiagStepTypeContinue;
			JsDiagSetStepType(step_type);
			break;
	}
}

static JsErrorCode CHAKRA_CALLBACK
on_fetch_dynamic_import(JsSourceContext importer, JsValueRef specifier, JsModuleRecord *out_module)
{
	return on_fetch_imported_module(NULL, specifier, out_module);
}

static JsErrorCode CHAKRA_CALLBACK
on_fetch_imported_module(JsModuleRecord importer, JsValueRef specifier, JsModuleRecord *out_module)
{
	// note: be careful, `importer` will be NULL if we were chained from
	//       on_fetch_dynamic_import().

	JsValueRef        caller_id;
	JsValueRef        exception;
	const char*       filename;
	bool              is_new_module;
	jsal_jmpbuf       label;
	int               last_stack_base;
	struct module_job job;
	JsModuleRecord    module;
	const char*       source;
	size_t            source_len;

	if (s_import_callback == NULL)
		return JsErrorInvalidArgument;

	last_stack_base = s_stack_base;
	s_stack_base = vector_len(s_value_stack);
	push_value(specifier, true);
	if (importer != NULL) {
		JsGetModuleHostInfo(importer, JsModuleHostInfo_HostDefined, &caller_id);
		push_value(caller_id, true);
	}
	else {
		jsal_push_null();
	}
	if (jsal_setjmp(label) == 0) {
		vector_push(s_catch_stack, label);
		s_import_callback();
		if (jsal_get_top() < 2)
			jsal_error(JS_TYPE_ERROR, "internal error in module callback");
		filename = jsal_require_string(-2);
		source = jsal_require_lstring(-1, &source_len);
		module = get_module_record(filename, importer, &is_new_module);
		if (is_new_module) {
			job.source_context = s_next_source_context++;
			job.module_record = module;
			job.source = strdup(source);
			job.source_size = strlen(source);
			vector_push(s_module_jobs, &job);
		}
		vector_pop(s_catch_stack, 1);
	}
	else {
		exception = pop_value();
		push_value(specifier, true);
		filename = jsal_get_string(-1);
		module = get_module_record(filename, importer, &is_new_module);
		JsSetModuleHostInfo(module, JsModuleHostInfo_Exception, exception);
		resize_stack(s_stack_base);
		s_stack_base = last_stack_base;
		*out_module = module;
		return JsNoError;
	}
	resize_stack(s_stack_base);
	s_stack_base = last_stack_base;
	*out_module = module;
	return JsNoError;
}

static void CHAKRA_CALLBACK
on_finalize_host_object(void* userdata)
{
	struct object* object_info;

	object_info = userdata;
	if (object_info->finalizer != NULL)
		object_info->finalizer(object_info->data);
	free(object_info);
}

static JsValueRef CHAKRA_CALLBACK
on_js_to_native_call(JsValueRef callee, bool is_ctor, JsValueRef argv[], unsigned short argc, void* userdata)
{
	JsValueRef       exception;
	struct function* function_data;
	bool             has_return;
	JsValueRef       last_callee_value;
	int              last_stack_base;
	JsValueRef       last_this_value;
	jsal_jmpbuf      label;
	JsValueRef       retval = JS_INVALID_REFERENCE;

	int i;

	function_data = userdata;

	last_stack_base = s_stack_base;
	last_callee_value = s_callee_value;
	last_this_value = s_this_value;
	s_stack_base = vector_len(s_value_stack);
	s_callee_value = callee;
	s_this_value = argv[0];
	for (i = 1; i < argc; ++i)
		push_value(argv[i], true);
	if (jsal_setjmp(label) == 0) {
		vector_push(s_catch_stack, label);
		if (!is_ctor && function_data->ctor_only) {
			push_value(callee, true);  // note: gets popped during unwind
			jsal_get_prop_string(-1, "name");
			jsal_error(JS_TYPE_ERROR, "constructor '%s()' requires 'new'", jsal_to_string(-1));
		}
		if (argc - 1 < function_data->min_args) {
			push_value(callee, true);  // note: gets popped during unwind
			jsal_get_prop_string(-1, "name");
			jsal_error(JS_TYPE_ERROR, "not enough arguments for '%s()'", jsal_to_string(-1));
		}
		has_return = function_data->callback(argc - 1, is_ctor, function_data->magic);
		if (has_return)
			retval = get_value(-1);
		vector_pop(s_catch_stack, 1);
	}
	else {
		// if an error gets thrown into C code, 'jsal_throw()' leaves it on top
		// of the value stack.
		exception = pop_value();
		JsSetException(exception);
		retval = exception;
	}
	resize_stack(s_stack_base);
	s_callee_value = last_callee_value;
	s_this_value = last_this_value;
	s_stack_base = last_stack_base;
	return retval;
}

static JsErrorCode CHAKRA_CALLBACK
on_notify_module_ready(JsModuleRecord module, JsValueRef exception)
{
	struct module_job job;

	memset(&job, 0, sizeof(struct module_job));
	job.module_record = module;
	job.source = NULL;
	vector_push(s_module_jobs, &job);
	return JsNoError;
}

static void CHAKRA_CALLBACK
on_resolve_reject_promise(JsValueRef task, void* userdata)
{
	JsValueRef  exception;
	jsal_jmpbuf label;
	int         last_stack_base;

	last_stack_base = s_stack_base;
	s_stack_base = vector_len(s_value_stack);
	push_value(task, true);
	if (jsal_setjmp(label) == 0) {
		vector_push(s_catch_stack, label);
		if (s_job_callback == NULL)
			jsal_error(JS_ERROR, "application is missing promise callback");
		s_job_callback();
		vector_pop(s_catch_stack, 1);
	}
	else {
		// if an error gets thrown into C code, 'jsal_throw()' leaves it on top
		// of the value stack.
		exception = pop_value();
		JsSetException(exception);
	}
	resize_stack(s_stack_base);
	s_stack_base = last_stack_base;
}

#if !defined(__APPLE__)
static int
asprintf(char** out, const char* format, ...)
{
	va_list ap;
	int     buf_size;

	va_start(ap, format);
	buf_size = vasprintf(out, format, ap);
	va_end(ap);
	return buf_size;
}

static int
vasprintf(char** out, const char* format, va_list ap)
{
	va_list apc;
	int     buf_size;

	va_copy(apc, ap);
	buf_size = vsnprintf(NULL, 0, format, apc) + 1;
	va_end(apc);
	*out = malloc(buf_size);
	vsnprintf(*out, buf_size, format, ap);
	return buf_size;
}
#endif
