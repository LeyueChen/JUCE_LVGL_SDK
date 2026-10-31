#include "rea/rea.h"
#include <algorithm>
#include <cmath>
#include <new>
#include <utility>
struct rea_chain {
	struct Node {
		const rea_node_type_t *type = nullptr;
		void *context = nullptr;
		bool bypass = false;
	};
	Node nodes[REA_MAX_NODES]{};
	const rea_node_type_t *types[REA_MAX_TYPES]{};
	unsigned count = 0, type_count = 0, channels = 0;
	uint32_t rate = 0;
};
namespace
{
struct Scalar {
	float value;
};
void gain_process(void *p, float *data, unsigned frames, unsigned channels)
{
	for (unsigned i = 0; i < frames * channels; ++i)
		data[i] *= static_cast<Scalar *>(p)->value;
}
void clip_process(void *p, float *data, unsigned frames, unsigned channels)
{
	for (unsigned i = 0; i < frames * channels; ++i)
		data[i] = std::tanh(data[i] * static_cast<Scalar *>(p)->value);
}
int gain_parameter(void *p, unsigned id, float v)
{
	if (id || !std::isfinite(v) || v < 0 || v > 4)
		return -1;
	static_cast<Scalar *>(p)->value = v;
	return 0;
}
int clip_parameter(void *p, unsigned id, float v)
{
	if (id || !std::isfinite(v) || v < 0.1f || v > 20)
		return -1;
	static_cast<Scalar *>(p)->value = v;
	return 0;
}
void scalar_destroy(void *p)
{
	delete static_cast<Scalar *>(p);
}
const rea_node_type_t gain = { REA_GAIN,     nullptr,	     nullptr,
			       gain_process, gain_parameter, scalar_destroy };
const rea_node_type_t clip = { REA_SOFT_CLIP, nullptr,	      nullptr,
			       clip_process,  clip_parameter, scalar_destroy };
int append_scalar(rea_chain_t *c, const rea_node_type_t &type, float v)
{
	Scalar s{};
	if (type.parameter(&s, 0, v))
		return -1;
	auto *state = new (std::nothrow) Scalar{ s };
	if (!state)
		return -1;
	int result = rea_chain_append(c, type.type_id, state);
	if (result < 0)
		delete state;
	return result;
}
}
extern "C" rea_chain_t *rea_chain_create(void)
{
	auto *chain = new (std::nothrow) rea_chain_t;
	if (chain) {
		rea_chain_register(chain, &gain);
		rea_chain_register(chain, &clip);
	}
	return chain;
}
extern "C" void rea_chain_clear(rea_chain_t *c)
{
	if (!c)
		return;
	for (unsigned i = 0; i < c->count; ++i) {
		auto &n = c->nodes[i];
		if (n.type->destroy)
			n.type->destroy(n.context);
		n = {};
	}
	c->count = 0;
}
extern "C" void rea_chain_destroy(rea_chain_t *c)
{
	rea_chain_clear(c);
	delete c;
}
extern "C" int rea_chain_register(rea_chain_t *c, const rea_node_type_t *type)
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
extern "C" int rea_chain_append(rea_chain_t *c, uint32_t id, void *context)
{
	if ((id == REA_GAIN || id == REA_SOFT_CLIP) && !context)
		return -1;
	if (!c || c->count == REA_MAX_NODES)
		return -1;
	for (unsigned i = 0; i < c->type_count; ++i)
		if (c->types[i]->type_id == id) {
			auto *type = c->types[i];
			if (c->rate && type->prepare &&
			    type->prepare(context, c->rate, c->channels))
				return -1;
			unsigned index = c->count++;
			c->nodes[index] = { type, context, false };
			return int(index);
		}
	return -1;
}
extern "C" int rea_chain_append_gain(rea_chain_t *c, float v)
{
	return append_scalar(c, gain, v);
}
extern "C" int rea_chain_append_soft_clip(rea_chain_t *c, float v)
{
	return append_scalar(c, clip, v);
}
extern "C" int rea_chain_parameter(rea_chain_t *c, unsigned i, unsigned p,
				   float v)
{
	if (!c || i >= c->count || !std::isfinite(v) ||
	    !c->nodes[i].type->parameter)
		return -1;
	return c->nodes[i].type->parameter(c->nodes[i].context, p, v);
}
extern "C" int rea_chain_bypass(rea_chain_t *c, unsigned i, int bypass)
{
	if (!c || i >= c->count)
		return -1;
	c->nodes[i].bypass = bypass != 0;
	return 0;
}
extern "C" int rea_chain_prepare(rea_chain_t *c, uint32_t rate,
				 unsigned channels)
{
	if (!c || !rate || channels == 0 || channels > 8)
		return -1;
	c->rate = 0;
	for (unsigned i = 0; i < c->count; ++i) {
		auto &n = c->nodes[i];
		if (n.type->prepare &&
		    n.type->prepare(n.context, rate, channels))
			return -1;
	}
	c->rate = rate;
	c->channels = channels;
	rea_chain_reset(c);
	return 0;
}
extern "C" void rea_chain_reset(rea_chain_t *c)
{
	if (c)
		for (unsigned i = 0; i < c->count; ++i)
			if (c->nodes[i].type->reset)
				c->nodes[i].type->reset(c->nodes[i].context);
}
extern "C" int rea_chain_process(rea_chain_t *c, float *data, unsigned frames,
				 unsigned channels)
{
	if (!c || !c->rate || !data || !frames || frames > 256 ||
	    c->channels != channels)
		return -1;
	for (unsigned i = 0; i < frames * channels; ++i)
		if (!std::isfinite(data[i]))
			data[i] = 0;
	for (unsigned i = 0; i < c->count; ++i)
		if (!c->nodes[i].bypass)
			c->nodes[i].type->process(c->nodes[i].context, data,
						  frames, channels);
	for (unsigned i = 0; i < frames * channels; ++i)
		data[i] = std::isfinite(data[i]) ?
				  std::clamp(data[i], -1.0f, 1.0f) :
				  0;
	return 0;
}
extern "C" int rea_chain_export(const rea_chain_t *c, rea_preset_t *p)
{
	if (!c || !p)
		return -1;
	rea_preset_t value{};
	value.version = 1;
	value.count = c->count;
	for (unsigned i = 0; i < c->count; ++i) {
		auto &n = c->nodes[i];
		if (n.type != &gain && n.type != &clip)
			return -1;
		value.nodes[i] = { n.type->type_id, n.bypass ? 1u : 0u,
				   static_cast<Scalar *>(n.context)->value };
	}
	*p = value;
	return 0;
}
extern "C" int rea_chain_import(rea_chain_t *c, const rea_preset_t *p)
{
	if (!c || !p || p->version != 1 || p->count > REA_MAX_NODES)
		return -1;
	auto *replacement = rea_chain_create();
	if (!replacement)
		return -1;
	bool ok = true;
	for (unsigned i = 0; i < p->count && ok; ++i) {
		const auto &n = p->nodes[i];
		int index =
			n.type == REA_GAIN ?
				rea_chain_append_gain(replacement, n.value) :
			n.type == REA_SOFT_CLIP ?
				rea_chain_append_soft_clip(replacement,
							   n.value) :
				-1;
		ok = index >= 0 && n.bypass <= 1;
		if (ok)
			rea_chain_bypass(replacement, i, int(n.bypass));
	}
	if (ok && c->rate)
		ok = rea_chain_prepare(replacement, c->rate, c->channels) == 0;
	if (ok) { // Preserve the application's type registry; only replace nodes.
		rea_chain_clear(c);
		c->count = replacement->count;
		std::copy_n(replacement->nodes, c->count, c->nodes);
		replacement->count = 0;
	}
	rea_chain_destroy(replacement);
	return ok ? 0 : -1;
}
