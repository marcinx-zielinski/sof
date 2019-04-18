/*
 * Copyright (c) 2016, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Intel Corporation nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
	   Artur Kloniecki <arturx.kloniecki@linux.intel.com>
 */

#include <config.h>

#if CONFIG_COMP_MUX

#include <stdint.h>
#include <stddef.h>
#include <sof/lock.h>
#include <sof/list.h>
#include <sof/stream.h>
#include <sof/audio/component.h>
#include <sof/ipc.h>
#include "mux.h"

// TODO: to be removed before release
#define print(x) trace_mux(#x " = %u", x)
#define printi(i, x) trace_mux(#x " = %u, i = %u", x, i)
#define printij(i, j, x) trace_mux(#x " = %u, i = %u, j = %u", x, i, j)

// TODO: to be removed before release
static void print_cfg_values(struct sof_mux_config *cfg)
{
	uint8_t i, j;

	print(cfg->mode);
	print(cfg->frame_format);
	print(cfg->num_channels);
	print(cfg->num_streams);

	for (i = 0; i < cfg->num_streams; i++) {
		printi(i, cfg->streams[i].num_channels);
		printi(i, cfg->streams[i].pipeline_id);
		for (j = 0; j < cfg->streams[i].num_channels; j++)
			printij(i, j, cfg->streams[i].mask[j]);
	}
}

static int mux_set_values(struct comp_data *cd, struct sof_mux_config *cfg)
{
	int i, j;

	// TODO: to be removed before release
	print_cfg_values(cfg);

	cd->mode = cfg->mode;
	cd->num_channels = cfg->num_channels;
	cd->frame_format = cfg->frame_format;

	for (i = 0; i < cfg->num_streams; i++) {
		cd->streams[i].num_channels = cfg->streams[i].num_channels;
		cd->streams[i].pipeline_id = cfg->streams[i].pipeline_id;
		for (j = 0; j < cfg->streams[i].num_channels; j++)
			cd->streams[i].mask[j] = cfg->streams[i].mask[j];
	}

	return 0;
}

static struct comp_dev *mux_new(struct sof_ipc_comp *comp)
{
	struct comp_dev *dev;
	struct sof_ipc_comp_mux *mux;
	struct sof_ipc_comp_mux *ipc_mux = (struct sof_ipc_comp_mux *)comp;
	struct comp_data *cd;

	trace_mux("mux_new()");

	if (IPC_IS_SIZE_INVALID(ipc_mux->config)) {
		IPC_SIZE_ERROR_TRACE(TRACE_CLASS_MUX, ipc_mux->config);
		return NULL;
	}

	dev = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM,
		      COMP_SIZE(struct sof_ipc_comp_mux));
	if (!dev)
		return NULL;

	mux = (struct sof_ipc_comp_mux *)&dev->comp;
	memcpy(mux, ipc_mux, sizeof(struct sof_ipc_comp_mux));

	cd = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM, sizeof(*cd));
	if (!cd) {
		rfree(dev);
		return NULL;
	}

	comp_set_drvdata(dev, cd);

	dev->state = COMP_STATE_READY;
	return dev;
}

static void mux_free(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);

	trace_mux("mux_free()");

	rfree(cd);
	rfree(dev);
}

/* set component audio stream parameters */
static int mux_params(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);

	trace_mux("mux_params()");

	dev->params.channels = cd->num_channels;
	dev->params.frame_fmt = cd->frame_format;

	return 0;
}

static int mux_ctrl_set_cmd(struct comp_dev *dev,
			    struct sof_ipc_ctrl_data *cdata)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct sof_mux_config *cfg;
	int ret = 0;

	trace_mux("mux_ctrl_set_cmd(), cdata->cmd = 0x%08x", cdata->cmd);

	switch (cdata->cmd) {
	case SOF_CTRL_CMD_BINARY:
		cfg = (struct sof_mux_config *)cdata->data->data;

		ret = mux_set_values(cd, cfg);
		break;
	default:
		trace_mux_error("mux_ctrl_set_cmd() error: invalid cdata->cmd ="
				" 0x%08x", cdata->cmd);
		ret = -EINVAL;
		break;
	}

	return ret;
}

/* used to pass standard and bespoke commands (with data) to component */
static int mux_cmd(struct comp_dev *dev, int cmd, void *data,
		   int max_data_size)
{
	struct sof_ipc_ctrl_data *cdata = data;

	trace_mux("mux_cmd() cmd = 0x%08x", cmd);

	switch (cmd) {
	case COMP_CMD_SET_DATA:
		return mux_ctrl_set_cmd(dev, cdata);
	default:
		return -EINVAL;
	}
}

static uint8_t get_stream_index(struct comp_data *cd, uint32_t pipe_id)
{
	int i;

	for (i = 0; i < MUX_MAX_STREAMS; i++) {
		if (cd->streams[i].pipeline_id == pipe_id)
			return i;
	}
	trace_mux_error("get_stream_index() error: couldn't find configuration "
			"for connected pipeline %u", pipe_id);
	return 0;
}

static inline int mux_process(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct comp_buffer *source;
	struct comp_buffer *sink;
	struct comp_buffer *sinks[MUX_MAX_STREAMS] = { NULL };
	struct list_item *clist;
	uint32_t num_sinks = 0;
	uint32_t i = 0;
	uint32_t frames = -1;
	uint32_t source_bytes;
	uint32_t sinks_bytes[MUX_MAX_STREAMS] = { 0 };

	tracev_mux("mux_process()");

	// align sink streams with their respective configurations
	list_for_item(clist, &dev->bsink_list) {
		sink = container_of(clist, struct comp_buffer, source_list);
		if (sink->sink->state == dev->state) {
			num_sinks++;
			i = get_stream_index(cd, sink->ipc_buffer.comp.pipeline_id);
			sinks[i] = sink;
		}
	}

	/* if there are no sinks active */
	if (num_sinks == 0)
		return 0;

	source = list_first_item(&dev->bsource_list, struct comp_buffer,
				 sink_list);

	/* check if source is active */
	if (source->source->state != dev->state)
		return 0;

	/* check for underrun */
	if (source->avail == 0) {
		trace_mux_error("mux_process() error: source component buffer "
				"has not enough data avaialble.");
		comp_underrun(dev, source, 0, 0);
		return -EIO;
	}

	/* check for overruns */
	/*for (i = 0; i < MUX_MAX_STREAMS; i++) {
		if (!sinks[i])
			continue;
		if (sinks[i]->free == 0)
		{
			trace_mux_error("mux_process() error: sink component "
					"buffer has not enough free bytes.");
			comp_overrun(dev, sinks[i], 0, 0);
			return -EIO;
		}
	}
	*/

	for (i = 0; i < MUX_MAX_STREAMS; i++) {
		if (!sinks[i])
			continue;
		frames = MIN(frames, comp_avail_frames(source, sinks[i]));
	}

	source_bytes = frames * comp_frame_bytes(source->source);
	for (i = 0; i < MUX_MAX_STREAMS; i++) {
		if (!sinks[i])
			continue;
		sinks_bytes[i] = frames * comp_frame_bytes(sinks[i]->sink);
	}

	/* produce output, one sink at a time */
	for (i = 0; i < MUX_MAX_STREAMS; i++) {
		if (!sinks[i])
			continue;

		cd->mux(dev, sinks[i], source, frames, &cd->streams[i]);
	}

	/* update components */
	for (i = 0; i < MUX_MAX_STREAMS; i++) {
		if (!sinks[i])
			continue;
		comp_update_buffer_produce(sinks[i], sinks_bytes[i]);
	}
	comp_update_buffer_consume(source, source_bytes);

	return 0;
}

static inline int demux_process(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct comp_buffer *sink;
	struct comp_buffer *source;
	struct comp_buffer *sources[MUX_MAX_STREAMS] = { NULL };
	struct list_item *clist;
	uint32_t num_sources = 0;
	uint32_t i = 0;
	uint32_t frames = -1;
	uint32_t sources_bytes[MUX_MAX_STREAMS] = { 0 };
	uint32_t sink_bytes;

	tracev_mux("demux_process()");

	// align source streams with their respective configurations
	list_for_item(clist, &dev->bsource_list) {
		source = container_of(clist, struct comp_buffer, sink_list);
		if (source->source->state == dev->state) {
			num_sources++;
			i = get_stream_index(cd, source->ipc_buffer.comp.pipeline_id);
			sources[i] = source;
		}
	}

	/* check if there are any sources active */
	if (num_sources == 0)
		return 0;

	sink = list_first_item(&dev->bsink_list, struct comp_buffer,
			       source_list);

	/* check if sink is active */
	if (sink->sink->state != dev->state)
		return 0;

	/* check for underrun */
	for (i = 0; i < MUX_MAX_STREAMS; i++) {
		if (!sources[i])
			continue;
		if (sources[i]->avail == 0) {
			trace_mux_error("demux_process() error: source "
					"component buffer has not enough data "
					"avaialble.");
			comp_underrun(dev, sources[i], 0, 0);
			return -EIO;
		}
	}

	/* check for overrun */
	if (sink->free == 0) {
		trace_mux_error("demux_process() error: sink component "
				"buffer has not enough free bytes.");
		comp_overrun(dev, sink, 0, 0);
		return -EIO;
	}

	for (i = 0; i < MUX_MAX_STREAMS; i++) {
		if (!sources[i])
			continue;
		frames = MIN(frames, comp_avail_frames(sources[i], sink));
	}

	for (i = 0; i < MUX_MAX_STREAMS; i++) {
		if (!sources[i])
			continue;
		sources_bytes[i] = frames *
				   comp_frame_bytes(sources[i]->source);
	}
	sink_bytes = frames * comp_frame_bytes(sink->sink);

	/* produce output */
	cd->demux(dev, sink, &sources[0], frames, &cd->streams[0]);

	/* update components */
	comp_update_buffer_produce(sink, sink_bytes);
	for (i = 0; i < MUX_MAX_STREAMS; i++) {
		if (!sources[i])
			continue;
		comp_update_buffer_consume(sources[i], sources_bytes[i]);
	}

	return 0;
}

/* copy and process stream data from source to sink buffers */
static int mux_copy(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	int ret = 0;

	tracev_mux("mux_copy()");

	if (cd->mode == MODE_MUX)
		ret = mux_process(dev);
	else
		ret = demux_process(dev);

	return ret;
}

static int mux_reset(struct comp_dev *dev)
{
	return 0;
}

static int mux_prepare(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	int ret;

	trace_mux("mux_prepare() mode = %u", cd->mode);

	ret = comp_set_state(dev, COMP_TRIGGER_PREPARE);
	if (ret) {
		trace_mux("mux_prepare() comp_set_state() returned non-zero.");
		return ret;
	}

	if (cd->mode == MODE_MUX) {
		cd->mux = mux_get_processing_function(dev);
		if (!cd->mux) {
			trace_mux_error("mux_prepare() error: couldn't find "
					"appropriate mux processing function "
					"for component.");
			ret = -EINVAL;
			goto err;
		}
	} else if (cd->mode == MODE_DEMUX) {
		cd->demux = demux_get_processing_function(dev);
		if (!cd->demux) {
			trace_mux_error("mux_prepare() error: couldn't find "
					"appropriate demux processing function "
					"for component.");
			ret = -EINVAL;
			goto err;
		}
	} else {
		trace_mux_error("mux_prepare() error: invalid mux mode set.");
		ret = -EINVAL;
		goto err;
	}

	return 0;

err:
	comp_set_state(dev, COMP_TRIGGER_RESET);
	return ret;
}

static int mux_trigger(struct comp_dev *dev, int cmd)
{
	trace_mux("mux_trigger(), command = %u", cmd);

	return comp_set_state(dev, cmd);
}

struct comp_driver comp_mux = {
	.type	= SOF_COMP_MUX,
	.ops	= {
		.new		= mux_new,
		.free		= mux_free,
		.params		= mux_params,
		.cmd		= mux_cmd,
		.copy		= mux_copy,
		.prepare	= mux_prepare,
		.reset		= mux_reset,
		.trigger	= mux_trigger,
	},
};

static void sys_comp_mux_init(void)
{
	comp_register(&comp_mux);
}

DECLARE_COMPONENT(sys_comp_mux_init);

#endif /* CONFIG_COMP_MUX */
