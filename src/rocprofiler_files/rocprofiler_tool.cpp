/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Yoann Heitz <yoann.heitz@polymtl.ca>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "barectf-platform-linux-fs.h"
#include "barectf.h"
#include "tracer.h"
#include "rocprofiler_tracers.h"
#include "rocprofiler_tool.h"
#include <time.h>

#define SECONDS_TO_NANOSECONDS 1000000000

barectf_platform_linux_fs_ctx *platform_metrics;
barectf_default_ctx *ctx_metrics;
uint64_t metrics_clock = 0;
uint64_t nb_events = 0;

const char *output_dir;
uint32_t dev_index = 0;
uint32_t verbose = 0;
bool kernel_event_initialized = false;
bool metrics_tables_dumped = false;
uint32_t metrics_number = 0;
uint32_t intermediate_metrics_number = 0;
const char **metrics_names;
uint64_t *metrics_values;
Kernel_Event_Tracer *kernel_event_tracer;
struct timespec tp;


//Initialize kernel events tracing
extern "C" void init_kernel_ctf_tracing(const char *prefix, std::vector<std::string> metrics_vector, uint32_t verbose_)
{
	if (!kernel_event_initialized)
	{
		output_dir = prefix;
		std::stringstream ss;
		ss << prefix << "/CTF_trace/" << GetPid() << "_metrics_stream";
		platform_metrics = barectf_platform_linux_fs_init(15000, ss.str().c_str(), 0, 0, 0, &metrics_clock);
		ctx_metrics = barectf_platform_linux_fs_get_barectf_ctx(platform_metrics);
		kernel_event_tracer = new Kernel_Event_Tracer(prefix, "kernel_events_");
		verbose = verbose_;
		metrics_number = metrics_vector.size();
		kernel_event_initialized = true;
	}
	else
	{
		printf("kernel events tracing already initialized");
	}
}

void write_nb_events()
{
	std::ostringstream outData;
	std::stringstream ss_metadata;
	ss_metadata << output_dir << "/CTF_trace/" << GetPid() << "_rocprofiler_nb_events.txt";
	outData << nb_events;
    std::ofstream out_file(ss_metadata.str());
    out_file << outData.str();
}

extern "C" void write_context_entry(context_entry_t *entry, const rocprofiler_dispatch_record_t *record, const std::string nik_name, const AgentInfo *agent_info)
{
	dev_index = agent_info->dev_index;
	kernel_event_tracer->write_context_entry(entry, record, nik_name, agent_info);
}

//Write a group of metrics
extern "C" void write_group(const context_entry_t *entry, const char *label)
{
	if (metrics_number > 0)
	{
		const rocprofiler_group_t *group = &(entry->group);
		if (!metrics_tables_dumped)
		{
			intermediate_metrics_number = group->feature_count;
			metrics_names = new const char *[intermediate_metrics_number + metrics_number];
			for (int i = 0; i < intermediate_metrics_number; i++)
			{
				metrics_names[i] = strdup(group->features[i]->name);
			}
		}
		metrics_values = new uint64_t[intermediate_metrics_number + metrics_number];
		for (int i = 0; i < intermediate_metrics_number; i++)
		{
			if (group->features[i]->data.kind == ROCPROFILER_DATA_KIND_INT64)
			{
				metrics_values[i] = group->features[i]->data.result_int64;
			}
			else
			{
				printf("Wrong data type : %u\n", group->features[i]->data.kind);
				abort();
			}
		}
	}
}

//Write metrics for a kernel event
extern "C" void write_metrics(const context_entry_t *entry, const char *label)
{
	if (metrics_number > 0)
	{
		int relative_idx = 0;
		if (!metrics_tables_dumped)
		{
			if (verbose == 0)
			{
				metrics_names = new const char *[metrics_number];
			}
			for (int i = intermediate_metrics_number; i < metrics_number + intermediate_metrics_number; i++)
			{
				relative_idx = i - intermediate_metrics_number;
				metrics_names[i] = strdup(entry->features[relative_idx].name);
			}
			
			clock_gettime(CLOCK_MONOTONIC, &tp);
			metrics_clock = SECONDS_TO_NANOSECONDS * tp.tv_sec + tp.tv_nsec;
			barectf_trace_metrics_table(ctx_metrics, metrics_number + intermediate_metrics_number, metrics_names);
			nb_events++;
			delete[] metrics_names;
			metrics_tables_dumped = true;
		}

		if (verbose == 0)
		{
			metrics_values = new uint64_t[metrics_number];
		}
		for (int i = intermediate_metrics_number; i < metrics_number + intermediate_metrics_number; i++)
		{
			relative_idx = i - intermediate_metrics_number;
			if (entry->features[relative_idx].data.kind == ROCPROFILER_DATA_KIND_INT64)
			{
				metrics_values[i] = entry->features[relative_idx].data.result_int64;
			}
			else
			{
				printf("Wrong data type : %u\n", entry->features[relative_idx].data.kind);
				abort();
			}
		}
		
		clock_gettime(CLOCK_MONOTONIC, &tp);
		metrics_clock = SECONDS_TO_NANOSECONDS * tp.tv_sec + tp.tv_nsec;
		barectf_trace_metrics_values(ctx_metrics, entry->index, dev_index, metrics_number + intermediate_metrics_number, metrics_values);
		nb_events++;
		delete[] metrics_values;
	}
}

extern "C" void unload_kernel_tracing()
{
	if (kernel_event_initialized)
	{
		kernel_event_tracer->flush((Tracer<kernel_event_t>::tracing_function)trace_kernel_event);
		barectf_platform_linux_fs_fini(platform_metrics);
		if (kernel_event_tracer != NULL)
		{
			nb_events += kernel_event_tracer->get_nb_events();
			write_nb_events();
			delete kernel_event_tracer;
		}
	}
}