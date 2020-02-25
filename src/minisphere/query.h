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

#ifndef SPHERE__QUERY_H__INCLUDED
#define SPHERE__QUERY_H__INCLUDED

typedef struct query query_t;

typedef
enum query_op
{
	QOP_NOP,
	QOP_BESIDES,
	QOP_DROP,
	QOP_OVER,
	QOP_PLUS,
	QOP_RANDOM,
	QOP_REVERSE,
	QOP_SAMPLE,
	QOP_SELECT,
	QOP_SHUFFLE,
	QOP_SORT_AZ,
	QOP_SORT_ZA,
	QOP_TAKE,
	QOP_TAP,
	QOP_THRU,
	QOP_UNIQ,
	QOP_WHERE,
	QOP_WITHOUT,
	QOP_MAX,
} query_op_t;

typedef
enum reduce_op
{
	ROP_NOP,
	ROP_AGGREGATE,
	ROP_ALL,
	ROP_ALL_IN,
	ROP_ANY,
	ROP_ANY_IN,
	ROP_ANY_IS,
	ROP_COUNT,
	ROP_FIND,
	ROP_FIND_KEY,
	ROP_FIRST,
	ROP_FOR_EACH,
	ROP_GROUP,
	ROP_ITERATOR,
	ROP_LAST,
	ROP_REMOVE,
	ROP_TO_ARRAY,
	ROP_UPDATE,
	ROP_WITHOUT,
	ROP_MAX,
} reduce_op_t;

query_t* query_new     (int num_sources);
query_t* query_clone   (const query_t* it);
query_t* query_ref     (query_t* it);
void     query_unref   (query_t* it);
void     query_add_op  (query_t* it, query_op_t opcode, js_ref_t* a);
void     query_run     (query_t* it, reduce_op_t opcode, js_ref_t* r1, js_ref_t* r2);

#endif // SPHERE__QUERY_H__INCLUDED
