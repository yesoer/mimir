#pragma once

#include <mim/def.h>
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
    Def2Def def_use_;                         // TODO : unscoped as of now, i.e. entries count globally
    GIDMap<const Def*, bool> type_is_linear_; // TODO : move gid map type to def.h when done

    void record_use(const Def* def, const Def* use) { def_use_[def] = use; }

    const Def* isa_find_use(const Def* def) {
        auto found = def_use_.find(def);
        return found == def_use_.end() ? nullptr : found->second;
    }

    /// iterates def based on its type to find linear components and call cb on
    /// their def part, returns whether a linear component was found
    template<typename F>
    bool for_each_linear_component(const Def* def, F&& cb) {
        // TODO : only/exactly here ?
        if (def->isa<Univ>() || def->isa_type<Univ>()) return false;

        auto type = def->unfold_type();
        std::cout << "for_each_linear_component : " << def << " type " << type << std::endl;

        if (is_linear_leaf(type)) {
            cb(def);
            type_is_linear_[type] = true;
            return true;
        }

        if (auto sig = type->isa<Sigma>()) {
            size_t n = sig->num_ops();
            for (size_t i = 0; i < n; ++i) {
                auto op_ty = sig->op(i);
                // skip types we previously determined to be non-linear
                // Note : just an optimization to prevent reoccuring unnecessary
                // recursions
                // TODO : definitely needs testing with dependent sigma
                auto found = type_is_linear_.find(op_ty);
                if (found != type_is_linear_.end() && !found->second) continue;

                auto proj            = def->proj(n, i);
                bool contains_linear = for_each_linear_component(proj, cb);
                if (!type_is_linear_[type]) type_is_linear_[type] = contains_linear;
            }
        }

        return type_is_linear_[type];

        // TODO : the others
    }

    bool is_linear_leaf(const Def* type);
    void register_production(const Def* def);
    Def* rewrite_mut(Def* mut) final;
    const Def* rewrite_imm(const Def* def) final;
    void finalize() final;
};

} // namespace mim::plug::mem::phase
