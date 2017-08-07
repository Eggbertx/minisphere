#ifndef MINISPHERE__SPHEREFS_H__INCLUDED
#define MINISPHERE__SPHEREFS_H__INCLUDED

#include "geometry.h"

typedef struct sandbox  sandbox_t;
typedef struct sfs_file sfs_file_t;

typedef
enum sfs_whence
{
	SFS_SEEK_SET,
	SFS_SEEK_CUR,
	SFS_SEEK_END,
} sfs_whence_t;

sandbox_t*       fs_new            (const char* pathname);
sandbox_t*       fs_ref            (sandbox_t* fs);
void             fs_free           (sandbox_t* fs);
int              fs_version        (const sandbox_t* fs);
const lstring_t* fs_manifest       (const sandbox_t* fs);
const char*      fs_author         (const sandbox_t* fs);
bool             fs_fullscreen     (const sandbox_t* fs);
const char*      fs_name           (const sandbox_t* fs);
const path_t*    fs_path           (const sandbox_t* fs);
size2_t          fs_resolution     (const sandbox_t* fs);
const char*      fs_summary        (const sandbox_t* fs);
const char*      fs_save_id        (const sandbox_t* fs);
const path_t*    fs_script_path    (const sandbox_t* fs);
vector_t*        fs_list_dir       (const sandbox_t* fs, const char* dirname, const char* base_dir, bool want_dirs);
path_t*          fs_build_path     (const char* filename, const char* base_dir_name, bool legacy);

bool        sfs_dir_exists (const sandbox_t* fs, const char* dirname, const char* base_dir);
sfs_file_t* sfs_fopen  (sandbox_t* fs, const char* path, const char* base_dir, const char* mode);
void        sfs_fclose (sfs_file_t* file);
bool        sfs_fexist (sandbox_t* fs, const char* filename, const char* base_dir);
const char* sfs_fpath  (sfs_file_t* file);
int         sfs_fputc  (int ch, sfs_file_t* file);
int         sfs_fputs  (const char* string, sfs_file_t* file);
size_t      sfs_fread  (void* buf, size_t size, size_t count, sfs_file_t* file);
bool        sfs_fseek  (sfs_file_t* file, long long offset, sfs_whence_t whence);
bool        sfs_fspew  (sandbox_t* fs, const char* filename, const char* base_dir, const void* buf, size_t size);
void*       sfs_fslurp (sandbox_t* fs, const char* filename, const char* base_dir, size_t *out_size);
long long   sfs_ftell  (sfs_file_t* file);
size_t      sfs_fwrite (const void* buf, size_t size, size_t count, sfs_file_t* file);
bool        sfs_mkdir  (sandbox_t* fs, const char* dirname, const char* base_dir);
bool        sfs_rmdir  (sandbox_t* fs, const char* dirname, const char* base_dir);
bool        sfs_rename (sandbox_t* fs, const char* filename1, const char* filename2, const char* base_dir);
bool        sfs_unlink (sandbox_t* fs, const char* filename, const char* base_dir);

#endif // MINISPHERE__SPHEREFS_H__INCLUDED
