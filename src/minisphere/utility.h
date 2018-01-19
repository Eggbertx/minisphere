/**
 *  miniSphere JavaScript game engine
 *  Copyright (c) 2015-2018, Fat Cerberus
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

#ifndef SPHERE__UTILITY_H__INCLUDED
#define SPHERE__UTILITY_H__INCLUDED

const path_t* assets_path (void);
const path_t* engine_path (void);
const path_t* home_path   (void);

bool        fread_rect16           (file_t* file, rect_t* out_rect);
bool        fread_rect32           (file_t* file, rect_t* out_rect);
int         jsal_push_lstring_t    (const lstring_t* string);
lstring_t*  jsal_require_lstring_t (int index);
const char* jsal_require_pathname  (int index, const char* origin_name, bool v1_mode, bool need_write);
const char* md5sum                 (const void* data, size_t size);
lstring_t*  read_lstring           (file_t* file, bool trim_null);
lstring_t*  read_lstring_raw       (file_t* file, size_t length, bool trim_null);
char*       strnewf                (const char* fmt, ...);
bool        write_lstring          (file_t* file, const lstring_t* string, bool include_nul);

#endif // SPHERE__UTILITY_H__INCLUDED
