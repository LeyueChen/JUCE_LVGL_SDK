#include "rea/rea.h"
#if REA_USE_RTNEURAL
#include <RTNeural/RTNeural.h>
#include <array>
#include <cmath>
#include <memory>
namespace
{
struct Neural {
	std::array<std::unique_ptr<RTNeural::Model<float> >, 8> models;
};
int prepare(void *p, uint32_t, unsigned channels)
{
	return p && channels <= 8 ? 0 : -1;
}
void reset(void *p)
{
	for (auto &m : static_cast<Neural *>(p)->models)
		m->reset();
}
void process(void *p, float *samples, unsigned frames, unsigned channels)
{
	auto &models = static_cast<Neural *>(p)->models;
	for (unsigned i = 0; i < frames; ++i)
		for (unsigned c = 0; c < channels; ++c) {
			float *sample = samples + i * channels + c;
			*sample = models[c]->forward(sample);
		}
}
void destroy(void *p)
{
	delete static_cast<Neural *>(p);
}
const rea_node_type_t type = { REA_RTNEURAL, prepare, reset,
			       process,	     nullptr, destroy };
bool validate(nlohmann::json &j)
{
	if (!j.is_object() || !j.contains("in_shape") ||
	    !j["in_shape"].is_array() || j["in_shape"].size() != 2 ||
	    j["in_shape"].back() != 1 || !j.contains("layers") ||
	    !j["layers"].is_array() || j["layers"].empty() ||
	    j["layers"].size() > 16)
		return false;
	unsigned input = 1;
	for (auto &layer : j["layers"]) {
		if (layer.at("type") != "dense" ||
		    !layer.at("shape").is_array() ||
		    layer["shape"].size() != 2 ||
		    !layer["shape"].back().is_number_integer())
			return false;
		int output = layer["shape"].back().get<int>();
		if (output < 1 || output > 64)
			return false;
		auto &w = layer.at("weights");
		if (!w.is_array() || w.size() != 2 || !w[0].is_array() ||
		    w[0].size() != input || !w[1].is_array() ||
		    w[1].size() != size_t(output))
			return false;
		for (auto &row : w[0]) {
			if (!row.is_array() || row.size() != size_t(output))
				return false;
			for (auto &v : row)
				if (!v.is_number() ||
				    !std::isfinite(v.get<float>()))
					return false;
		}
		for (auto &v : w[1])
			if (!v.is_number() || !std::isfinite(v.get<float>()))
				return false;
		const auto activation =
			layer.value("activation", std::string{});
		if (activation == "linear")
			layer["activation"] = "";
		else if (activation != "" && activation != "tanh" &&
			 activation != "relu" && activation != "sigmoid")
			return false;
		input = unsigned(output);
	}
	return input == 1;
}
}
#endif
extern "C" int rea_chain_append_rtneural(rea_chain_t *chain, const char *text,
					 size_t size)
{
#if REA_USE_RTNEURAL
	if (!chain || !text || !size || size > 262144)
		return -1;
	try {
		auto json = nlohmann::json::parse(text, text + size);
		if (!validate(json))
			return -1;
		auto state = std::make_unique<Neural>();
		for (auto &model : state->models) {
			model = RTNeural::json_parser::parseJson<float>(json);
			if (!model || model->layers.empty() ||
			    model->getInSize() != 1 || model->getOutSize() != 1)
				return -1;
			model->reset();
		}
		if (rea_chain_register(chain, &type))
			return -1;
		int index = rea_chain_append(chain, REA_RTNEURAL, state.get());
		if (index >= 0)
			state.release();
		return index;
	} catch (...) {
		return -1;
	}
#else
	(void)chain;
	(void)text;
	(void)size;
	return -1;
#endif
}
