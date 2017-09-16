/**
 *  SSj, the Sphere JavaScript debugger
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

#include "ssj.h"
#include "source.h"

struct source
{
	vector_t* lines;
};

static char*
read_line(const char** p_string)
{
	char*  buffer;
	size_t buf_size;
	char   ch;
	bool   have_line = false;
	size_t length;

	buffer = malloc(buf_size = 256);
	length = 0;
	while (!have_line) {
		if (length + 1 >= buf_size)
			buffer = realloc(buffer, buf_size *= 2);
		if ((ch = *(*p_string)) == '\0')
			goto hit_eof;
		++(*p_string);
		switch (ch) {
		case '\n': have_line = true; break;
		case '\r':
			have_line = true;
			if (*(*p_string) == '\n')  // CR LF?
				++(*p_string);
			break;
		default:
			buffer[length++] = ch;
		}
	}

hit_eof:
	buffer[length] = '\0';
	if (*(*p_string) == '\0' && length == 0) {
		free(buffer);
		return NULL;
	}
	else
		return buffer;
}

source_t*
source_new(const char* text)
{
	char*       line_text;
	vector_t*   lines;
	source_t*   source = NULL;
	const char* p_source;

	p_source = text;
	lines = vector_new(sizeof(char*));
	while (line_text = read_line(&p_source))
		vector_push(lines, &line_text);

	source = calloc(1, sizeof(source_t));
	source->lines = lines;
	return source;
}

void
source_free(source_t* source)
{
	iter_t it;
	char*  *p_line;

	if (source == NULL) return;
	it = vector_enum(source->lines);
	while (p_line = iter_next(&it))
		free(*p_line);
	vector_free(source->lines);
	free(source);
}

int
source_cloc(const source_t* source)
{
	return (int)vector_len(source->lines);
}

const char*
source_get_line(const source_t* source, int line_index)
{
	if (line_index < 0 || line_index >= source_cloc(source))
		return NULL;
	return *(char**)vector_get(source->lines, line_index);
}

void
source_print(const source_t* source, int lineno, int num_lines, int active_lineno)
{
	const char* arrow;
	int         line_count;
	int         median;
	int         start, end;
	const char* text;

	int i;

	line_count = source_cloc(source);
	median = num_lines / 2;
	start = lineno > median ? lineno - (median + 1) : 0;
	end = start + num_lines < line_count ? start + num_lines : line_count;
	for (i = start; i < end; ++i) {
		text = source_get_line(source, i);
		arrow = i + 1 == active_lineno ? "=>" : "  ";
		if (num_lines == 1)
			printf("%d %s\n", i + 1, text);
		else {
			if (i + 1 == active_lineno)
				printf("\33[36;1m");
			printf("%s %4d %s\n", arrow, i + 1, text);
			printf("\33[m");
		}
	}
}
