#include "subst_plugin.hpp"

namespace eosio {

    static auto _subst_plugin = application::register_plugin<subst_plugin>();

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
        fc::http_client httpc;

        fc::sha256 store_code(std::vector<uint8_t> new_code) {
            auto new_hash = fc::sha256::hash((const char*)new_code.data(), new_code.size());
            cache.codes[new_hash] = std::move(new_code);
            return new_hash;
        }

        void subst(const fc::sha256 old_hash, std::vector<uint8_t> new_code) {
            fc::sha256 new_hash = store_code(new_code);
            cache.substitutions[old_hash] = new_hash;
        }

        void subst(const eosio::name account_name, std::vector<uint8_t> new_code) {
            fc::sha256 new_hash = store_code(new_code);
            cache.substitutions_by_name[account_name.to_uint64_t()] = new_hash;
        }

        void subst(const std::string& subst_info, std::vector<uint8_t> new_code) {
            // update substitution maps
            if (subst_info.size() < 16) {
                // if smaller than 16 char assume its an account name
                subst(eosio::name(subst_info), new_code);
            } else {
                // if not assume its a code hash
                subst(fc::sha256(subst_info), new_code);
            }
        }

        void load_remote_manifest(std::string chain_id, fc::url manifest_url) {
            string upath = manifest_url.path()->generic_string();

            if (!boost::algorithm::ends_with(upath, "subst.json"))
                wlog("Looks like provided url based substitution manifest"
                        "doesn\'t end with \"susbt.json\"... trying anyways...");

            fc::variant manifest = httpc.get_sync_json(manifest_url);
            auto& manif_obj = manifest.get_object();

            auto it = manif_obj.find(chain_id);
            if (it != manif_obj.end()) {
                for (auto subst_entry : (*it).value().get_object()) {
                    bpath url_path = *(manifest_url.path());
                    auto wasm_url_path = url_path.remove_filename() / chain_id / subst_entry.value().get_string();

                    auto wasm_url = fc::url(
                        manifest_url.proto(), manifest_url.host(), manifest_url.user(), manifest_url.pass(),
                        wasm_url_path,
                        manifest_url.query(), manifest_url.args(), manifest_url.port()
                    );

                    std::vector<uint8_t> new_code = httpc.get_sync_raw(wasm_url);
                    subst(fc::sha256(subst_entry.key()), new_code);
                }
            } else {
                ilog("Manifest found but chain id not present.");
            }
        }
    };  // subst_plugin_impl

    subst_plugin::subst_plugin() :
        my(std::make_shared<subst_plugin_impl>())
    {}

    subst_plugin::~subst_plugin() {}

    void subst_plugin::set_program_options(options_description& cli, options_description& cfg) {
        auto options = cfg.add_options();
        options(
            "subst", bpo::value<vector<string>>()->composing(),
            "contract_hash:new_contract.wasm. Whenever the contrac with the hash \"contract_hash\""
            "needs to run, substitute debug.wasm in "
            "its place and enable debugging support. This bypasses size limits, timer limits, and "
            "other constraints on debug.wasm. nodeos still enforces constraints on contract.wasm. "
            "(may specify multiple times)");
    }

    void subst_plugin::plugin_initialize(const variables_map& options) {
        auto* chain_plug = app().find_plugin<chain_plugin>();
        auto& control = chain_plug->chain();
        std::string chain_id = control.get_chain_id();
        try {
            if (options.count("subst")) {
                auto substs = options.at("subst").as<vector<string>>();
                for (auto& s : substs) {

                    auto manifest_url = fc::url(s);
                    if (manifest_url.path().has_value()) {
                        my->load_remote_manifest(chain_id, manifest_url);
                    } else {
                        std::vector<std::string> v;
                        boost::split(v, s, boost::is_any_of(":"));

                        EOS_ASSERT(
                            v.size() == 2,
                            fc::invalid_arg_exception,
                            "Invalid value ${s} for --subst", ("s", s)
                        );

                        auto susbt_info = v[0];
                        auto new_code_path = v[1];

                        std::vector<uint8_t> new_code = eosio::vm::read_wasm(new_code_path);
                        my->subst(susbt_info, new_code);
                    }
                }
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
