#pragma once

#include <unordered_map>

#include <mim/phase.h>

namespace mim::plug::mem::phase {

/// Verifies linearity of mem objects
class MemLinearity : public mim::Analysis {
public:
    MemLinearity(World& world)
        : mim::Analysis(world, "MemLinearity") {}

    MemLinearity(World& world, flags_t annex)
        : mim::Analysis(world, annex) {}

private:
    std::unordered_map<const Def*, const Def*> def_use_map_;

    void record_use(const Def* def, const Def* use) { def_use_map_[def] = use; }

    const Def* isa_find_use(const Def* def) {
        auto found = def_use_map_.find(def);
        return found == def_use_map_.end() ? nullptr : found->second;
    }

    Def* rewrite_mut(Def* mut) final;
    const Def* rewrite_imm_App(const App* old_app) final;
    void finalize() final;
};

} // namespace mim::plug::mem::phase
