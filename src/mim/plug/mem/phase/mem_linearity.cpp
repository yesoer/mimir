#include "mim/plug/mem/phase/mem_linearity.h"

#include "mim/plug/mem/mem.h"

namespace mim::plug::mem::phase {

Def* MemLinearity::rewrite_mut(Def* mut) {
    if (!lookup(mut)) {
        if (auto lam = mut->isa_mut<Lam>()) {
            // TODO : should we put this in rewrite_mut_Lam or another helper ?
            for (auto param : lam->vars()) {
                if (plug::mem::isa_mem(param)) {
                    if (!isa_find_use(param)) record_use(param, param);
                }
            }
        }
    }
    return Analysis::rewrite_mut(mut);
}

const Def* MemLinearity::rewrite_imm_App(const App* old_app) {
    auto arg    = old_app->arg();
    auto callee = old_app->callee();

    rewrite(arg);
    rewrite(callee);

    auto check_and_consume = [&](const Def* mem_val) {
        auto found = isa_find_use(mem_val);
        if (!found || found != mem_val) {
            auto err = found ? "attempted reuse of linear object" : "attempted use of unavailable object";
            mem_val->blame("{}", err).bail();
        }
        record_use(mem_val, old_app);
    };

    if (auto arg_sig = arg->type()->isa<Sigma>()) {
        size_t n = arg_sig->num_ops();
        for (size_t i = 0; i < n; ++i) {
            auto proj = arg->proj(n, i);
            if (plug::mem::isa_mem(proj)) check_and_consume(arg->proj(n, i));
        }
    } else if (plug::mem::isa_mem(arg)) {
        check_and_consume(arg);
    }

    if (auto sig = old_app->type()->isa<Sigma>()) {
        size_t n = sig->num_ops();
        for (size_t i = 0; i < n; ++i) {
            auto proj = old_app->proj(n, i);
            if (plug::mem::isa_mem(proj)) {
                if (!isa_find_use(proj)) record_use(proj, proj);
            }
        }
    } else if (plug::mem::isa_mem(old_app)) {
        record_use(old_app, old_app);
    }

    return old_app;
}

void MemLinearity::finalize() {
    for (auto [def, use] : def_use_map_)
        if (def == use) def->blame("linear object was never used").bail();
}

} // namespace mim::plug::mem::phase
