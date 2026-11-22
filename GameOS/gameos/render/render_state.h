// SPDX-License-Identifier: GPL-3.0-only
#ifndef GOS_RENDER_STATE_H
#define GOS_RENDER_STATE_H

// Backend independent description of the fixed-function state a draw call
// needs, derived from the GameOS render states (gos_State_*).
//
// The state that affects pipeline objects (blending, depth, culling, alpha
// test) is packed into a 32-bit PipelineKey, so a backend can cache one
// pipeline object per key (SDL_GPU) or skip redundant state changes (GL).
// Sampler state (filter, address mode) is a separate SamplerKey because
// samplers are bound per texture, not per pipeline.

#include <stddef.h>
#include <stdint.h>
#include <unordered_map>

namespace gos_render {

enum class BlendMode : uint8_t {
    Opaque,          // gos_Alpha_OneZero      (blending off)
    Additive,        // gos_Alpha_OneOne
    AlphaBlend,      // gos_Alpha_AlphaInvAlpha
    Premultiplied,   // gos_Alpha_OneInvAlpha
    AlphaAdditive,   // gos_Alpha_AlphaOne
    Count
};

enum class CullMode : uint8_t { None, Back, Front, Count }; // gos_Cull_None / CW / CCW

enum class DepthTest : uint8_t {
    Off,          // gos_State_ZCompare 0 (depth test disabled)
    LessEqual,    // 1
    Less,         // 2
    Count
};

struct PipelineDesc {
    BlendMode blend = BlendMode::Opaque;
    CullMode cull = CullMode::None;
    DepthTest depth_test = DepthTest::LessEqual;
    bool depth_write = true;
    bool alpha_test = false; // discard alpha == 0 (selects a shader variant)

    bool operator==(const PipelineDesc& o) const {
        return blend == o.blend && cull == o.cull && depth_test == o.depth_test &&
               depth_write == o.depth_write && alpha_test == o.alpha_test;
    }
    bool operator!=(const PipelineDesc& o) const { return !(*this == o); }
};

typedef uint32_t PipelineKey;

// Unique key for every distinct PipelineDesc (and only for valid ones).
PipelineKey packPipelineKey(const PipelineDesc& d);
// Inverse of packPipelineKey; returns false for keys it never produces.
bool unpackPipelineKey(PipelineKey key, PipelineDesc* out);

enum class FilterMode : uint8_t { Nearest, Linear, Count };
enum class AddressMode : uint8_t { Wrap, Clamp, Count };

struct SamplerDesc {
    FilterMode filter = FilterMode::Nearest;
    AddressMode address = AddressMode::Wrap;
    bool operator==(const SamplerDesc& o) const { return filter == o.filter && address == o.address; }
};

typedef uint32_t SamplerKey;
SamplerKey packSamplerKey(const SamplerDesc& d);

// Translates the GameOS render state array (indexed by gos_State_*; at least
// gos_MaxState entries) into backend independent descriptions. Unknown
// values fall back to the default and set *valid to false if given.
PipelineDesc pipelineFromGosStates(const uint32_t* states, bool* valid = nullptr);
SamplerDesc samplerFromGosStates(const uint32_t* states);

// Key -> backend object cache with statistics. The factory is called once
// per distinct key; objects are destroyed by the owner via forEach().
template <typename T>
class PipelineCache {
public:
    template <typename Factory>
    T& get(PipelineKey key, Factory make) {
        typename std::unordered_map<PipelineKey, T>::iterator it = cache_.find(key);
        if (it != cache_.end()) {
            ++hits_;
            return it->second;
        }
        ++misses_;
        return cache_.emplace(key, make(key)).first->second;
    }
    bool contains(PipelineKey key) const { return cache_.count(key) != 0; }
    size_t size() const { return cache_.size(); }
    uint64_t hits() const { return hits_; }
    uint64_t misses() const { return misses_; }
    template <typename F> void forEach(F f) {
        for (auto& kv : cache_) f(kv.first, kv.second);
    }
    void clear() { cache_.clear(); hits_ = misses_ = 0; }

private:
    std::unordered_map<PipelineKey, T> cache_;
    uint64_t hits_ = 0;
    uint64_t misses_ = 0;
};

} // namespace gos_render

#endif
