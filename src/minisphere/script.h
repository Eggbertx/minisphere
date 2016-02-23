#ifndef MINISPHERE__SCRIPT_H__INCLUDED
#define MINISPHERE__SCRIPT_H__INCLUDED

typedef struct script script_t;

extern void             initialize_scripts (void);
extern void             shutdown_scripts   (void);
extern const lstring_t* get_source_text    (const char* filename);
extern bool             evaluate_script    (const char* filename);
extern script_t*        compile_script     (const lstring_t* script, const char* fmt_name, ...);
extern script_t*        ref_script         (script_t* script);
extern void             free_script        (script_t* script);
extern void             run_script         (script_t* script, bool allow_reentry);

extern script_t* duk_require_sphere_script (duk_context* ctx, duk_idx_t index, const char* name);

#endif // MINISPHERE__SCRIPT_H__INCLUDED
