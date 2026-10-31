#include "rea/rea.h"
#include <stdbool.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	const rea_node_type_t *type;
	void *context;
	bool bypass;
} rea_node_t;

struct rea_chain {
	rea_node_t nodes[REA_MAX_NODES];
	const rea_node_type_t *types[REA_MAX_TYPES];
	unsigned count, type_count, channels;
	uint32_t rate;
};

typedef struct {
	float value;
} rea_scalar_t;

static void gain_process(void *p, float *data, unsigned frames,
			 unsigned channels)
{
	for (unsigned i = 0; i < frames * channels; ++i)
		data[i] *= ((rea_scalar_t *)p)->value;
}

static void clip_process(void *p, float *data, unsigned frames,
			 unsigned channels)
{
	for (unsigned i = 0; i < frames * channels; ++i)
		data[i] = tanhf(data[i] * ((rea_scalar_t *)p)->value);
}

static int gain_parameter(void *p, unsigned id, float v)
{
	if (id || !isfinite(v) || v < 0 || v > 4)
		return -1;
	((rea_scalar_t *)p)->value = v;
	return 0;
}

static int clip_parameter(void *p, unsigned id, float v)
{
	if (id || !isfinite(v) || v < 0.1f || v > 20)
		return -1;
	((rea_scalar_t *)p)->value = v;
	return 0;
}

static void scalar_destroy(void *p)
{
	free(p);
}

static const rea_node_type_t gain = { REA_GAIN,	      NULL,
				      NULL,	      gain_process,
				      gain_parameter, scalar_destroy };
static const rea_node_type_t clip = { REA_SOFT_CLIP,  NULL,
				      NULL,	      clip_process,
				      clip_parameter, scalar_destroy };

static int append_scalar(rea_chain_t *c, const rea_node_type_t *type, float v)
{
	rea_scalar_t value = { 0 };
	if (type->parameter(&value, 0, v))
		return -1;
	rea_scalar_t *state = malloc(sizeof(*state));
	if (!state)
		return -1;
	*state = value;
	int result = rea_chain_append(c, type->type_id, state);
	if (result < 0)
		free(state);
	return result;
}

rea_chain_t *rea_chain_create(void)
{
	rea_chain_t *chain = calloc(1, sizeof(*chain));
	if (chain) {
		rea_chain_register(chain, &gain);
		rea_chain_register(chain, &clip);
	}
	return chain;
}

void rea_chain_clear(rea_chain_t *c)
{
	if (!c)
		return;
	for (unsigned i = 0; i < c->count; ++i) {
		rea_node_t *node = &c->nodes[i];
		if (node->type->destroy)
			node->type->destroy(node->context);
		*node = (rea_node_t){ 0 };
	}
	c->count = 0;
}

void rea_chain_destroy(rea_chain_t *c)
{
	rea_chain_clear(c);
	free(c);
}

int rea_chain_register(rea_chain_t *c, const rea_node_type_t *type)
{
	if (!c || !type || !type->type_id || !type->process)
		return -1;
	for (unsigned i = 0; i < c->type_count; ++i)
		if (c->types[i]->type_id == type->type_id)
			return c->types[i] == type ? 0 : -1;
	if (c->type_count == REA_MAX_TYPES)
		return -1;
	c->types[c->type_count++] = type;
	return 0;
}

int rea_chain_append(rea_chain_t *c, uint32_t id, void *context)
{
	if ((id == REA_GAIN || id == REA_SOFT_CLIP) && !context)
		return -1;
	if (!c || c->count == REA_MAX_NODES)
		return -1;
	for (unsigned i = 0; i < c->type_count; ++i)
		if (c->types[i]->type_id == id) {
			const rea_node_type_t *type = c->types[i];
			if (c->rate && type->prepare &&
			    type->prepare(context, c->rate, c->channels))
				return -1;
			unsigned index = c->count++;
			c->nodes[index] = (rea_node_t){ type, context, false };
			return (int)index;
		}
	return -1;
}

int rea_chain_append_gain(rea_chain_t *c, float v)
{
	return append_scalar(c, &gain, v);
}

int rea_chain_append_soft_clip(rea_chain_t *c, float v)
{
	return append_scalar(c, &clip, v);
}

int rea_chain_parameter(rea_chain_t *c, unsigned i, unsigned p, float v)
{
	if (!c || i >= c->count || !isfinite(v) || !c->nodes[i].type->parameter)
		return -1;
	return c->nodes[i].type->parameter(c->nodes[i].context, p, v);
}

int rea_chain_bypass(rea_chain_t *c, unsigned i, int bypass)
{
	if (!c || i >= c->count)
		return -1;
	c->nodes[i].bypass = bypass != 0;
	return 0;
}

int rea_chain_prepare(rea_chain_t *c, uint32_t rate, unsigned channels)
{
	if (!c || !rate || channels == 0 || channels > 8)
		return -1;
	c->rate = 0;
	for (unsigned i = 0; i < c->count; ++i) {
		rea_node_t *node = &c->nodes[i];
		if (node->type->prepare &&
		    node->type->prepare(node->context, rate, channels))
			return -1;
	}
	c->rate = rate;
	c->channels = channels;
	rea_chain_reset(c);
	return 0;
}

void rea_chain_reset(rea_chain_t *c)
{
	if (c)
		for (unsigned i = 0; i < c->count; ++i)
			if (c->nodes[i].type->reset)
				c->nodes[i].type->reset(c->nodes[i].context);
}

int rea_chain_process(rea_chain_t *c, float *data, unsigned frames,
		      unsigned channels)
{
	if (!c || !c->rate || !data || !frames || frames > 256 ||
	    c->channels != channels)
		return -1;
	for (unsigned i = 0; i < frames * channels; ++i)
		if (!isfinite(data[i]))
			data[i] = 0;
	for (unsigned i = 0; i < c->count; ++i)
		if (!c->nodes[i].bypass)
			c->nodes[i].type->process(c->nodes[i].context, data,
						  frames, channels);
	for (unsigned i = 0; i < frames * channels; ++i) {
		float value = data[i];
		data[i] = !isfinite(value) ? 0 :
			  value < -1.0f	   ? -1.0f :
			  value > 1.0f	   ? 1.0f :
					     value;
	}
	return 0;
}

int rea_chain_export(const rea_chain_t *c, rea_preset_t *p)
{
	if (!c || !p)
		return -1;
	rea_preset_t value = { 0 };
	value.version = 1;
	value.count = c->count;
	for (unsigned i = 0; i < c->count; ++i) {
		const rea_node_t *node = &c->nodes[i];
		if (node->type != &gain && node->type != &clip)
			return -1;
		value.nodes[i] = (rea_preset_node_t){
			node->type->type_id, node->bypass ? 1u : 0u,
			((const rea_scalar_t *)node->context)->value
		};
	}
	*p = value;
	return 0;
}

int rea_chain_import(rea_chain_t *c, const rea_preset_t *p)
{
	if (!c || !p || p->version != 1 || p->count > REA_MAX_NODES)
		return -1;
	rea_chain_t *replacement = rea_chain_create();
	if (!replacement)
		return -1;
	bool ok = true;
	for (unsigned i = 0; i < p->count && ok; ++i) {
		const rea_preset_node_t *node = &p->nodes[i];
		int index = node->type == REA_GAIN ?
				    rea_chain_append_gain(replacement,
							  node->value) :
			    node->type == REA_SOFT_CLIP ?
				    rea_chain_append_soft_clip(replacement,
							       node->value) :
				    -1;
		ok = index >= 0 && node->bypass <= 1;
		if (ok)
			rea_chain_bypass(replacement, i, (int)node->bypass);
	}
	if (ok && c->rate)
		ok = rea_chain_prepare(replacement, c->rate, c->channels) == 0;
	if (ok) {
		/* Preserve the application's type registry; only replace nodes. */
		rea_chain_clear(c);
		c->count = replacement->count;
		memcpy(c->nodes, replacement->nodes,
		       c->count * sizeof(c->nodes[0]));
		replacement->count = 0;
	}
	rea_chain_destroy(replacement);
	return ok ? 0 : -1;
}
