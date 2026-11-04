#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine.hpp"

namespace mnky::audio::fx {

/// Browser metadata for one effect, matching the host's plugin-browser shape.
struct EffectInfo {
    std::string id;
    std::string manufacturer;
    std::string type;
    std::string category;
    std::string name;
};

/// Instance-owned factory registry (deliberately not a singleton). Built-in
/// effects are registered by the constructor; `add()` lets an embedder expose
/// additional creators without static-initialisation order problems.
class Registry {
public:
    using Creator = std::function<std::unique_ptr<IEffect>()>;

    Registry();

    /// Registers one effect. Fails for an empty/duplicate/invalid id, a null
    /// creator, or empty browser metadata. On success `list()` is re-sorted by
    /// category then name then id.
    bool add(EffectInfo info, Creator creator);

    /// nullptr for an unknown id or a creator that returns nullptr. A built-in
    /// result is an Effect, so state/session code can dynamic_cast to it.
    std::unique_ptr<IEffect> create(const std::string& id) const;

    /// Metadata for `id`, or nullptr when it is not registered.
    const EffectInfo* info(const std::string& id) const;

    bool contains(const std::string& id) const;

    /// All registered effects, sorted for a browser (category, name, id).
    const std::vector<EffectInfo>& list() const { return mList; }

    size_t size() const { return mList.size(); }

private:
    std::vector<EffectInfo> mList;
    std::unordered_map<std::string, Creator> mCreators;
};

}  // namespace mnky::audio::fx
