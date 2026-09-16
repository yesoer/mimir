#include "mim/plug/mem/phase/mem_linearity.h"

#include <mim/axm.h>

#include "mim/plug/mem/mem.h"

namespace mim::plug::mem::phase {

bool MemLinearity::is_linear_leaf(const Def* type) {
    assert(type->isa_type<Type>());

    // TODO : later refactor to property on def objects, setable through 'linear' keyword in mim
    return Axm::isa<mem::M>(type);
}

/// recurse through the def's components and register all linear ones as
/// available in the def_use_ map
void MemLinearity::register_production(const Def* def) {
    for_each_linear_component(def, [&](const Def* lin_def) {
        if (!isa_find_use(lin_def)) record_use(lin_def, lin_def);
    });
}

Def* MemLinearity::rewrite_mut(Def* mut) {
    if (!lookup(mut)) {
        if (auto lam = mut->isa_mut<Lam>())
            for (auto param : lam->vars())
                register_production(param);
    }
    return Analysis::rewrite_mut(mut);
}

const Def* MemLinearity::rewrite_imm(const Def* def) {
    auto res = Rewriter::rewrite_imm(def);
    if (res->isa_type<Type>()) return res; // skip type level defs

    // TODO : is it correct that only apps are considered consuming ?
    // before I wanted e.g. tuple construction to be that as well but
    // since we do not propagate linearity to the tuple anymore (because
    // that would require a change in how tuples can be deconstructed) and
    // have fine grained tracking instead, it seems only app is needed anymore
    if (auto app = def->isa<App>()) {
        // TODO : consider wrapping this in a register_consumption
        for (auto op : app->ops())
            for_each_linear_component(op, [&](const Def* lin_def) {
                auto found = isa_find_use(lin_def);
                if (!found || found != lin_def) {
                    auto err = found ? "attempted reuse of linear object" : "attempted use of unavailable object";
                    lin_def->blame("{} `{}`", err, lin_def).bail();
                }
                record_use(lin_def, app);
            });
    }

    register_production(def);
    return res;
}

void MemLinearity::finalize() {
    for (auto [def, use] : def_use_)
        if (def == use) def->blame("linear object `{}` was never used", def).bail();
}

} // namespace mim::plug::mem::phase
