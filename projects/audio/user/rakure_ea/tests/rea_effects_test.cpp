#include "rea/rea.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
static void check(bool ok)
{
	if (!ok)
		std::abort();
}
int main()
{
	auto *chain = rea_chain_create();
	check(chain);
	check(rea_chain_append_gain(chain, 0.5f) == 0);
	check(!rea_chain_prepare(chain, 48000, 2));
	float samples[] = { 0.8f, -0.8f, 0.4f, -0.4f };
	check(!rea_chain_process(chain, samples, 2, 2));
	check(std::abs(samples[0] - 0.4f) < 1e-6);
	check(!rea_chain_parameter(chain, 0, 0, 2));
	check(rea_chain_parameter(chain, 0, 0,
				  std::numeric_limits<float>::quiet_NaN()) < 0);
	check(!rea_chain_bypass(chain, 0, 1));
	check(!rea_chain_process(chain, samples, 2, 2) &&
	      std::abs(samples[0] - 0.4f) < 1e-6);
	check(rea_chain_append_soft_clip(chain, 3) == 1);
	rea_preset_t preset{};
	check(!rea_chain_export(chain, &preset));
	check(!rea_chain_import(chain, &preset));
	auto bad = preset;
	bad.nodes[0].type = 999;
	check(rea_chain_import(chain, &bad) < 0);
	rea_preset_t retained{};
	check(!rea_chain_export(chain, &retained));
	check(retained.count == preset.count &&
	      retained.nodes[0].type == preset.nodes[0].type);
	samples[0] = 0.4f;
	check(!rea_chain_process(chain, samples, 2, 2));
	check(std::abs(samples[0] - std::tanh(1.2f)) < 1e-6);
	check(rea_chain_process(chain, samples, 2, 1) < 0);
	rea_chain_clear(chain);
#if REA_USE_RTNEURAL
	const char model[] =
		R"({"in_shape":[null,1],"layers":[{"type":"dense","shape":[null,1],"activation":"linear","weights":[[[0.5]],[0.1]]}]})";
	check(rea_chain_append_rtneural(chain, model, sizeof(model) - 1) == 0);
	check(rea_chain_append_rtneural(chain, "{}", 2) < 0);
	const char malformed[] =
		R"({"in_shape":[null,1],"layers":[{"type":"dense","shape":[null,1],"weights":[[[1]],[]]}]})";
	check(rea_chain_append_rtneural(chain, malformed,
					sizeof(malformed) - 1) < 0);
	check(!rea_chain_prepare(chain, 48000, 2));
	samples[0] = 0.8f;
	samples[1] = -0.8f;
	check(!rea_chain_process(chain, samples, 1, 2));
	check(std::abs(samples[0] - 0.5f) < 1e-6 &&
	      std::abs(samples[1] + 0.3f) < 1e-6);
#endif
	rea_chain_destroy(chain);
	std::puts("PASS effect lifecycle/parameters/bypass/presets/RTNeural");
}
