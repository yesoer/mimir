#pragma once

#include <mim/def.h>
#include <mim/phase.h>
#include <mim/tuple.h>

namespace mim::plug::mem::phase {

/// Verifies linearity of mem objects
// TODO : Research : test with multiple distinct mem objects, find references via :Ag mem.alloc (.*, [^0])
// TODO : Error : no hash consing for nodes that produce/consume linear stuff
//        (and those that wrap them/have them as operands)
//        -> Test : consing_linearity.mim
//        -> See world.h insert method, right before the stamp call, I think
//           the hash_combine is to make sure hashes differ for mutables, which
//           would be the same what we need but can we safely reuse mut_ or
//           should we have a new flag on defs i.e. lin_ ?
// -> with QTT later on we could consider allowing hash-consing and track on the
//    node how many times it was written i.e.consed, but the resource still needs
//    to be generate multiple times by the backend, so we only reduce the IR size
//    and extend all def nodes with this count, which considering the amount of
//    work might be a bad tradeoff
class MemLinearity : public mim::Analysis {
public:
    MemLinearity(World& world)
        : mim::Analysis(world, "MemLinearity") {}

    MemLinearity(World& world, flags_t annex)
        : mim::Analysis(world, annex) {}

private:
    Def2Def def_use_; // TODO : Error : unscoped as of now, i.e. entries count
                      //        globally -> problem with same mem object
                      //        being consumed in two branches
                      // -> Test : linearity_scope_conflict.mim

    // types can give us information whether there is any way that the
    // corresponding term MAY produce/consume, but a consumer/producer type
    // does not mean the corresponding term is a consumer/producer !
    // i.e. (3, True)#idx : Nat u Bool tells us there is nothing linear
    // but ((True, mem1), (False, mem2))#idx#1I0 : mem.M 0 does not mean the
    // term is the producer/consumer, that requires recursing down to mem1 and
    // mem2
    // so type can be used to avoid recursing down where there is no linearity
    // but if there is any in the type, we have to look at the term
    // TODO : Refactor : maybe the type_contains_linear_ lookup should be once
    // at the start of for_each_linear_component ?
    GIDMap<const Def*, bool> type_contains_linear_; // TODO : Refactor : move gid map type to def.h when done

    void record_use(const Def* def, const Def* use) { def_use_[def] = use; }

    const Def* isa_find_use(const Def* def) {
        auto found = def_use_.find(def);
        return found == def_use_.end() ? nullptr : found->second;
    }

    void register_consumption(const Def* possibly_consumed, const Def* consumer);

    /// takes an extract and determines what elements can be reached based
    /// on the indexing
    // TODO : Refactor : first recurse completely, then check leaf : tuple vs var etc.
    bool resolve_extract(const Def* def, [[maybe_unused]] DefVec& out) {
        auto extract = def->isa<Extract>();
        assert(extract);

        auto tuple = extract->tuple();
        auto idx   = extract->index();

        auto isa_arr = tuple->isa<Arr>();

        auto push_proj = [&]() {
            if (auto lit_idx = Lit::isa(idx)) {
                auto n    = tuple->num_projs();
                auto proj = tuple->proj(n, lit_idx.value());
                out.push_back(proj);
            } else {
                out = tuple->projs();
            }
        };

        if (tuple->isa<Tuple>() || (isa_arr && Lit::isa(isa_arr->arity()))) {
            // recursion anchor : enumerable
            push_proj();
            return false;
        } else if (tuple->isa<Var>() || tuple->isa<App>()) {
            // recursion anchor : atomic nodes/not reducible
            push_proj();
            return true;
        } else if (auto nested_extract = tuple->isa<Extract>()) {
            DefVec in;
            auto has_atomic_tuple = resolve_extract(nested_extract, in);

            if (auto lit_idx = Lit::isa(idx)) {
                auto idx = lit_idx.value();
                for (auto in_tuple : in) {
                    auto n    = in_tuple->num_projs();
                    auto proj = in_tuple->proj(n, idx);
                    out.push_back(proj);
                }

                return has_atomic_tuple;
                ;
            } else {
                // unwrap once e.g. the extract was ((a,b),(c,d))#idx#idx
                // the recursion anchor for tuple gets ((a,b),(c,d))#idx
                // and gives us in = [(a,b),(c,d)]
                // => out = [a,b,c,d]
                for (auto in_tuple : in)
                    for (auto proj : in_tuple->projs())
                        out.push_back(proj);
                return has_atomic_tuple;
            }
        } else {
            // TODO : Error : non enumerable arrays, including insert operations etc.
            // , call cb directly and add nothing or
            // set the bool and add the array (ignoring any projections)
            // unsure about the insert
            // -> Test : mem/variable_size_array_linear.mim
            // TODO : other cases
            std::cerr << "not implemented resolve_extract case " << tuple->node_name() << std::endl;
            return false;
        }
    }

    /// iterates def to find linear components and call cb on
    /// their def part, returns whether a linear component was found
    /// since types can be derived further than terms, in some cases we want to
    /// switch based on the term e.g. extract, to reach the core linear elements
    /// to call cb on :
    /// ((True, mem1), (True, mem2))#idx#1I1 : mem.M 0
    /// these handlings must come before the others
    ///
    /// Note : atm I expect them to be exactly all term elimination nodes
    /// Note : ofc that means if ... else if ... structure for the function
    ///
    /// afterwards we can switch by type
    template<typename F>
    bool for_each_linear_component(const Def* def, F&& cb) {
        auto type = def->unfold_type();

        // TODO : Refactor : use type_contains_linear_
        auto extract = def->isa<Extract>();
        if (extract) {
            DefVec out;
            bool has_atomic_tuple = resolve_extract(extract, out);

            bool any = false;
            if (has_atomic_tuple) {
                for (auto opt : out) {
                    if (is_linear_leaf(opt->unfold_type())) {
                        cb(opt);
                        any = true;
                    }
                }
            } else {
                DefSet seen;
                for (auto opt : out) {
                    // TODO : Error : we could have (mem, true) and (mem, false) -> seen does not recognize that which
                    // will cause problems, should probably be possible to pass seen to for_each_linear_component
                    // -> Test : nested_duplicate_runtime_index.mim
                    if (seen.emplace(opt).second) any |= for_each_linear_component(opt, cb);
                }
            }
            return any;
        }

        else if (type->isa<Sigma>()) {
            size_t n = def->num_projs();
            for (size_t i = 0; i < n; ++i) {
                auto proj = def->proj(n, i);

                auto proj_ty = proj->unfold_type();
                auto found   = type_contains_linear_.find(proj_ty);
                if (found != type_contains_linear_.end() && !found->second) continue;

                bool contains_linear = for_each_linear_component(proj, cb);
                type_contains_linear_[type] |= contains_linear;
            }
        }

        else if (type->isa<Reform>())
            // TODO : Ask : I believe Reform is unavailable from mim, what about
            // meet and uniq, have they been removed ?
            assert(false && "Reform not implemented");
        else if (type->isa<Pi>()) {
            // TODO : Error : check if the lambda captures linear stuff i.e.
            // check its free vars that is relevant if its the arg to an
            // application but also if its the callee of an application right ?
            // so maybe a separate function should be written that we can call
            // from here (for args) and from rewrite_imm for the callee ?
            std::cerr << "Pi not implemented for_each_linear_component case def " << def << " node name "
                      << def->node_name() << " : " << type << " node name " << type->node_name() << std::endl;
        } else if (type->isa<Nat>() || type->isa<Idx>()) {
            type_contains_linear_[type] = false;
        } else if (is_linear_leaf(type)) {
            cb(def);
            type_contains_linear_[type] = true;
        } else {
            std::cerr << "not implemented for_each_linear_component case def " << def << " node name "
                      << def->node_name() << " : " << type << " node name " << type->node_name() << std::endl;
        }

        return type_contains_linear_[type];
    }

    bool is_linear_leaf(const Def* type);
    void register_production(const Def* def);
    Def* rewrite_mut(Def* mut) final;
    const Def* rewrite_imm(const Def* def) final;
    void finalize() final;
};

} // namespace mim::plug::mem::phase
