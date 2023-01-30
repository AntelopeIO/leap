#include "subst_plugin.hpp"

#include <boost/algorithm/string.hpp>
#include <debug_eos_vm/debug_contract.hpp>
#include <eosio/chain/transaction_context.hpp>


namespace eosio {

    static appbase::abstract_plugin& _subst_plugin = app().register_plugin<subst_plugin>();

    struct debug_vm_options {
        static constexpr std::uint32_t max_call_depth = 1024;
    };

    DEBUG_PARSE_CODE_SECTION(eosio::chain::eos_vm_host_functions_t, debug_vm_options)
    using debug_contract_backend = eosio::vm::backend<
        eosio::chain::eos_vm_host_functions_t,
        eosio::vm::jit_profile,
        debug_vm_options,
        debug_eos_vm::debug_instr_map
    >;

    struct subst_plugin_impl : std::enable_shared_from_this<subst_plugin_impl> {
        debug_contract::substitution_cache<debug_contract_backend> cache;

        void subst(const std::string& old_code_hash, const std::string& new_code_path) {
            std::vector<uint8_t> new_code;

            new_code = eosio::vm::read_wasm(new_code_path);

            wlog("===================SUBST-PLUGIN==================");
            wlog("Loaded new wasm for ${h}", ("h", old_code_hash));
            wlog("New size: ${s}", ("s", new_code.size()));
            wlog("=================================================");

            auto new_hash = fc::sha256::hash((const char*)new_code.data(), new_code.size());
            cache.substitutions[old_code_hash] = new_hash;
            cache.codes[new_hash] = std::move(new_code);
        }
    };  // subst_plugin_impl

    subst_plugin::subst_plugin() : my(std::make_shared<subst_plugin_impl>()) {}

    subst_plugin::~subst_plugin() {}

    void subst_plugin::set_program_options(options_description& cli, options_description& cfg) {
        auto options = cfg.add_options();
        cfg.add_options()(
            "subst", bpo::value<vector<string>>()->composing(),
            "contract_hash:new_contract.wasm. Whenever the contrac with the hash \"contract_hash\""
            "needs to run, substitute debug.wasm in "
            "its place and enable debugging support. This bypasses size limits, timer limits, and "
            "other constraints on debug.wasm. nodeos still enforces constraints on contract.wasm. "
            "(may specify multiple times)");
    }

    void subst_plugin::plugin_initialize(const variables_map& options) {
        try {
            if (options.count("subst")) {
                auto substs = options.at("subst").as<vector<string>>();
                for (auto& s : substs) {
                    std::vector<std::string> v;
                    boost::split(v, s, boost::is_any_of(":"));
                    EOS_ASSERT(v.size() == 2, fc::invalid_arg_exception,
                                "Invalid value ${s} for --subst", ("s", s));
                    my->subst(v[0], v[1]);
                }
                auto* chain_plug = app().find_plugin<chain_plugin>();
                auto& control = chain_plug->chain();
                auto& iface = control.get_wasm_interface();
                iface.substitute_apply = [this](
                    const eosio::chain::digest_type& code_hash,
                    uint8_t vm_type, uint8_t vm_version,
                    eosio::chain::apply_context& context
                ) {
                    auto timer_pause =
                        fc::make_scoped_exit([&]() { context.trx_context.resume_billing_timer(); });
                    context.trx_context.pause_billing_timer();
                    return my->cache.substitute_apply(code_hash, vm_type, vm_version, context);
                };
            }
        }
        FC_LOG_AND_RETHROW()
    }  // subst_plugin::plugin_initialize

    void subst_plugin::plugin_startup() {}

    void subst_plugin::plugin_shutdown() {}

}  // namespace eosio
