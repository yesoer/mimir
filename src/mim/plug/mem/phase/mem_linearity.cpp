#include "mim/plug/mem/phase/mem_linearity.h"

#include <mim/axm.h>

#include "mim/plug/mem/mem.h"

namespace mim::plug::mem::phase {

bool MemLinearity::is_linear_leaf(const Def* type) {
    assert(type->isa_type<Type>());
    return Axm::isa<mem::M>(type);
}

/// recurse through the def's components and register all linear ones as
/// available in the def_use_ map
void MemLinearity::register_production(const Def* def) {
    for_each_linear_component(def, [&](const Def* lin_def) {
        // nodes may be visited multiple times but can only produce once
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

void MemLinearity::register_consumption(const Def* possibly_consumed, const Def* consumer) {
    for_each_linear_component(possibly_consumed, [&](const Def* lin_def) {
        auto found = isa_find_use(lin_def);
        if (!found || found != lin_def) {
            if (found)
                found->blame("attempted reuse of linear object `{}`", lin_def).bail();

            else
                lin_def->blame("attempted use of unavailable object `{}`", lin_def).bail();
        }
        record_use(lin_def, consumer);
    });
}

const Def* MemLinearity::rewrite_imm(const Def* def) {
    Rewriter::rewrite_imm(def); // rewrite operands first

    if (def->isa_type<Type>()) return def;                     // type level can't produce/consume
    if (def->isa<Univ>() || def->isa_type<Univ>()) return def; // universal level can't produce/consume

    // Note : only applications are considered consuming
    if (auto app = def->isa<App>())
        for (auto arg : app->args())
            register_consumption(arg, app);

    register_production(def);

    return def;
}

void MemLinearity::finalize() {
    for (auto [def, use] : def_use_)
        if (def == use) def->blame("linear object `{}` was never used", def).bail();
}

} // namespace mim::plug::mem::phase
