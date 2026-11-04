#include "registry.hpp"

#include <algorithm>
#include <utility>

#include "comp.hpp"
#include "delay.hpp"
#include "eq.hpp"
#include "reverb.hpp"

namespace mnky::audio::fx {
namespace {

bool validId(const std::string& id) {
    if (id.empty()) return false;
    for (const unsigned char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    return true;
}

bool validInfo(const EffectInfo& info) {
    return validId(info.id) && !info.manufacturer.empty() && !info.type.empty() && !info.category.empty() &&
           !info.name.empty();
}

bool browserLess(const EffectInfo& left, const EffectInfo& right) {
    if (left.category != right.category) return left.category < right.category;
    if (left.name != right.name) return left.name < right.name;
    return left.id < right.id;
}

}  // namespace

Registry::Registry() {
    (void)add({"eq", "mnkybox", "effect", "EQ", "Parametric EQ"},
              []() -> std::unique_ptr<IEffect> { return std::make_unique<Eq>(); });
    (void)add({"comp", "mnkybox", "effect", "Dynamics", "Compressor"},
              []() -> std::unique_ptr<IEffect> { return std::make_unique<Comp>(); });
    (void)add({"delay", "mnkybox", "effect", "Delay", "Stereo Delay"},
              []() -> std::unique_ptr<IEffect> { return std::make_unique<Delay>(); });
    (void)add({"reverb", "mnkybox", "effect", "Reverb", "Reverb"},
              []() -> std::unique_ptr<IEffect> { return std::make_unique<Reverb>(); });
}

bool Registry::add(EffectInfo info, Creator creator) {
    if (!validInfo(info) || !creator) return false;
    if (mCreators.find(info.id) != mCreators.end()) return false;
    mCreators.emplace(info.id, std::move(creator));
    mList.push_back(std::move(info));
    std::sort(mList.begin(), mList.end(), browserLess);
    return true;
}

std::unique_ptr<IEffect> Registry::create(const std::string& id) const {
    const auto it = mCreators.find(id);
    if (it == mCreators.end()) return nullptr;
    return it->second();
}

const EffectInfo* Registry::info(const std::string& id) const {
    for (const EffectInfo& entry : mList) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

bool Registry::contains(const std::string& id) const {
    return mCreators.find(id) != mCreators.end();
}

}  // namespace mnky::audio::fx
