// Tests for the backend independent render state (GameOS/gameos/render/render_state).

#include "mc2test.h"

#include "gameos.hpp"
#include "render/render_state.h"

#include <set>
#include <string>

using namespace gos_render;

static void defaultStates(uint32_t* s)
{
    for (int i = 0; i < gos_MaxState; ++i)
        s[i] = 0;
    s[gos_State_AlphaMode] = gos_Alpha_OneZero;
    s[gos_State_Culling] = gos_Cull_None;
    s[gos_State_ZCompare] = 1;
    s[gos_State_ZWrite] = 1;
    s[gos_State_Filter] = gos_FilterNone;
    s[gos_State_TextureAddress] = gos_TextureWrap;
}

TEST(pipeline_key_round_trips_every_valid_desc)
{
    std::set<PipelineKey> keys;
    for (int blend = 0; blend < (int)BlendMode::Count; ++blend)
        for (int cull = 0; cull < (int)CullMode::Count; ++cull)
            for (int depth = 0; depth < (int)DepthTest::Count; ++depth)
                for (int zw = 0; zw < 2; ++zw)
                    for (int at = 0; at < 2; ++at) {
                        PipelineDesc d;
                        d.blend = (BlendMode)blend;
                        d.cull = (CullMode)cull;
                        d.depth_test = (DepthTest)depth;
                        d.depth_write = zw;
                        d.alpha_test = at;
                        const PipelineKey k = packPipelineKey(d);
                        keys.insert(k);
                        PipelineDesc back;
                        REQUIRE(unpackPipelineKey(k, &back));
                        CHECK(back == d);
                    }
    CHECK_EQ(keys.size(), (size_t)(5 * 3 * 3 * 2 * 2)); // all distinct
}

TEST(pipeline_key_rejects_keys_it_never_produces)
{
    CHECK(!unpackPipelineKey(7, nullptr));          // blend out of range
    CHECK(!unpackPipelineKey(3u << 3, nullptr));    // cull out of range
    CHECK(!unpackPipelineKey(3u << 5, nullptr));    // depth out of range
    CHECK(!unpackPipelineKey(1u << 9, nullptr));    // unused bits set
    CHECK(unpackPipelineKey(0, nullptr));
}

TEST(gos_states_map_to_pipeline)
{
    uint32_t s[gos_MaxState];
    defaultStates(s);
    bool valid = false;
    PipelineDesc d = pipelineFromGosStates(s, &valid);
    CHECK(valid);
    CHECK(d.blend == BlendMode::Opaque);
    CHECK(d.cull == CullMode::None);
    CHECK(d.depth_test == DepthTest::LessEqual);
    CHECK(d.depth_write);
    CHECK(!d.alpha_test);

    s[gos_State_AlphaMode] = gos_Alpha_AlphaInvAlpha;
    s[gos_State_Culling] = gos_Cull_CW;   // clockwise faces culled = back faces
    s[gos_State_ZCompare] = 0;
    s[gos_State_ZWrite] = 0;
    s[gos_State_AlphaTest] = 1;
    d = pipelineFromGosStates(s, &valid);
    CHECK(valid);
    CHECK(d.blend == BlendMode::AlphaBlend);
    CHECK(d.cull == CullMode::Back);
    CHECK(d.depth_test == DepthTest::Off);
    CHECK(!d.depth_write);
    CHECK(d.alpha_test);

    s[gos_State_Culling] = gos_Cull_CCW;
    s[gos_State_ZCompare] = 2;
    s[gos_State_AlphaMode] = gos_Alpha_OneOne;
    d = pipelineFromGosStates(s, &valid);
    CHECK(d.cull == CullMode::Front);
    CHECK(d.depth_test == DepthTest::Less);
    CHECK(d.blend == BlendMode::Additive);

    s[gos_State_AlphaMode] = 12345; // garbage is reported
    pipelineFromGosStates(s, &valid);
    CHECK(!valid);
}

TEST(states_that_do_not_affect_pipelines_do_not_change_the_key)
{
    uint32_t a[gos_MaxState], b[gos_MaxState];
    defaultStates(a);
    defaultStates(b);
    b[gos_State_Texture] = 42;
    b[gos_State_Fog] = 0xff00ff00;
    b[gos_State_Filter] = gos_FilterBiLinear;
    b[gos_State_Specular] = 1;
    CHECK_EQ(packPipelineKey(pipelineFromGosStates(a)), packPipelineKey(pipelineFromGosStates(b)));
}

TEST(sampler_desc_from_states)
{
    uint32_t s[gos_MaxState];
    defaultStates(s);
    SamplerDesc d = samplerFromGosStates(s);
    CHECK(d.filter == FilterMode::Nearest);
    CHECK(d.address == AddressMode::Wrap);
    s[gos_State_Filter] = gos_FilterTriLinear; // no mipmaps: linear
    s[gos_State_TextureAddress] = gos_TextureClamp;
    d = samplerFromGosStates(s);
    CHECK(d.filter == FilterMode::Linear);
    CHECK(d.address == AddressMode::Clamp);
    CHECK(packSamplerKey(d) != packSamplerKey(SamplerDesc()));
}

TEST(pipeline_cache_creates_once_per_key)
{
    PipelineCache<std::string> cache;
    int created = 0;
    auto make = [&](PipelineKey k) { ++created; return "pipeline " + std::to_string(k); };
    CHECK(cache.get(5, make) == "pipeline 5");
    CHECK(cache.get(5, make) == "pipeline 5");
    CHECK(cache.get(9, make) == "pipeline 9");
    CHECK_EQ(created, 2);
    CHECK_EQ(cache.size(), (size_t)2);
    CHECK_EQ(cache.hits(), (uint64_t)1);
    CHECK_EQ(cache.misses(), (uint64_t)2);
    CHECK(cache.contains(9));
    int visited = 0;
    cache.forEach([&](PipelineKey, std::string&) { ++visited; });
    CHECK_EQ(visited, 2);
    cache.clear();
    CHECK_EQ(cache.size(), (size_t)0);
}
