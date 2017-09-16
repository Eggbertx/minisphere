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
#include "async.h"

#include "script.h"
#include "vector.h"

struct job
{
	bool         armed;
	bool         finished;
	async_hint_t hint;
	double       priority;
	uint32_t     timer;
	int64_t      token;
	script_t*    script;
};

static int sort_jobs (const void* in_a, const void* in_b);

static bool      s_need_sort = false;
static int64_t   s_next_token = 1;
static vector_t* s_onetime;
static vector_t* s_recurring;

void
async_init(void)
{
	console_log(1, "initializing dispatch manager");
	s_onetime = vector_new(sizeof(job_t*));
	s_recurring = vector_new(sizeof(job_t*));
}

void
async_uninit(void)
{
	console_log(1, "shutting down dispatch manager");
	vector_free(s_onetime);
	vector_free(s_recurring);
}

bool
async_busy(void)
{
	return vector_len(s_recurring) > 0
		|| vector_len(s_onetime) > 0;
}

void
async_cancel_all(bool recurring)
{
	iter_t iter;
	job_t* job;

	iter = vector_enum(s_onetime);
	while (iter_next(&iter)) {
		job = *(job_t**)iter.ptr;
		job->finished = true;
	}

	if (recurring) {
		iter = vector_enum(s_recurring);
		while (iter_next(&iter)) {
			job = *(job_t**)iter.ptr;
			job->finished = true;
		}
		s_need_sort = true;
	}
}

void
async_cancel(int64_t token)
{
	iter_t  iter;
	job_t** p_job;

	iter = vector_enum(s_onetime);
	while (p_job = iter_next(&iter)) {
		if ((*p_job)->token == token)
			(*p_job)->finished = true;
	}

	iter = vector_enum(s_recurring);
	while (p_job = iter_next(&iter)) {
		if ((*p_job)->token == token)
			(*p_job)->finished = true;
	}

	s_need_sort = true;
}

int64_t
async_defer(script_t* script, uint32_t timeout, async_hint_t hint)
{
	job_t* job;

	if (s_onetime == NULL)
		return 0;
	job = calloc(1, sizeof(job_t));
	job->timer = timeout;
	job->token = s_next_token++;
	job->script = script;
	job->hint = hint;
	vector_push(s_onetime, &job);
	return job->token;
}

int64_t
async_recur(script_t* script, double priority, async_hint_t hint)
{
	job_t* job;

	if (s_recurring == NULL)
		return 0;
	if (hint == ASYNC_RENDER) {
		// invert priority for render jobs.  this ensures higher priority jobs
		// get rendered later in a frame, i.e. closer to the screen.
		priority = -priority;
	}
	job = calloc(1, sizeof(job_t));
	job->token = s_next_token++;
	job->script = script;
	job->hint = hint;
	job->priority = priority;
	vector_push(s_recurring, &job);

	s_need_sort = true;

	return job->token;
}

void
async_run_jobs(async_hint_t hint)
{
	iter_t iter;
	job_t* job;

	if (s_need_sort)
		vector_sort(s_recurring, sort_jobs);

	// process recurring jobs
	iter = vector_enum(s_recurring);
	while (iter_next(&iter)) {
		job = *(job_t**)iter.ptr;
		if (job->hint == hint && !job->finished)
			script_run(job->script, true);
		if (job->finished) {
			script_unref(job->script);
			iter_remove(&iter);
		}
	}

	// process one-time jobs.  swap in a fresh queue first to allow nested callbacks
	// to work.
	if (s_onetime != NULL) {
		iter = vector_enum(s_onetime);
		while (iter_next(&iter)) {
			job = *(job_t**)iter.ptr;
			if (!job->armed) {
				// avoid starting jobs on the same tick they're dispatched.  this works reliably
				// because one-time jobs are always added to the end of the list.
				job->armed = true;
				continue;  // defer till next tick
			}
			if (job->hint == hint && job->timer-- == 0 && !job->finished) {
				script_run(job->script, false);
				job->finished = true;
			}
			if (job->finished) {
				script_unref(job->script);
				iter_remove(&iter);
			}
		}
	}
}

static int
sort_jobs(const void* in_a, const void* in_b)
{
	// qsort() is not stable.  luckily job tokens are strictly sequential,
	// so we can maintain FIFO order by just using the token as part of the
	// sort key.

	job_t*  job_a;
	job_t*  job_b;
	double  delta = 0.0;
	int64_t fifo_delta;

	job_a = *(job_t**)in_a;
	job_b = *(job_t**)in_b;
	delta = job_b->priority - job_a->priority;
	fifo_delta = job_a->token - job_b->token;
	return delta < 0.0 ? -1 : delta > 0.0 ? 1
		: fifo_delta < 0 ? -1 : fifo_delta > 0 ? 1
		: 0;
}
