#include "subst_plugin.hpp"

namespace eosio {

    static auto _subst_plugin = application::register_plugin<subst_plugin>();

    struct subst_plugin_impl : std::enable_shared_from_this<subst_plugin_impl> {

        std::map<fc::sha256, fc::sha256> substitutions;
        std::map<fc::sha256, uint32_t> sub_from;
        std::map<fc::sha256, std::vector<uint8_t>> codes;

        std::set<eosio::name> target_names;
        std::map<fc::sha256, fc::sha256> enabled_substitutions;

        chainbase::database* db;

        fc::http_client httpc;
        appbase::variables_map app_options;

        void debug_print_maps() {
            // print susbtitution maps for debug
            ilog("loaded substitutions:");
            for (auto it = substitutions.begin();
                it != substitutions.end(); it++) {
                auto key = it->first;
                auto new_hash = it->second;
                auto bnum_it = sub_from.find(key);
                if (bnum_it == sub_from.end()) {
                    ilog(
                        "${k} -> ${new_hash}",
                        ("k", key)("new_hash", new_hash)
                    );
                } else {
                    ilog(
                        "${k} -> ${new_hash} from block ${from}",
                        ("k", key)("new_hash", new_hash)("from", bnum_it->second)
                    );
                }
            }
        }

        void perform_replacement(
            fc::sha256 og_hash,
            fc::sha256 new_hash,
            uint8_t vm_type,
            uint8_t vm_version,
            eosio::chain::apply_context& context
        ) {

            const chain::code_object* target_entry = db->find<chain::code_object, chain::by_code_hash>(
                boost::make_tuple(og_hash, vm_type, vm_type));

            EOS_ASSERT(
                target_entry,
                fc::invalid_arg_exception,
                "target entry for substitution doesn't exist"
            );

            auto code = codes[new_hash];

            db->modify(*target_entry, [&](chain::code_object& o) {
                o.code.assign(code.data(), code.size());
                o.vm_type = 0;
                o.vm_version = 0;
            });

            target_names.insert(context.get_receiver());
            enabled_substitutions[og_hash] = new_hash;
        }

        bool substitute_apply(
            const fc::sha256& code_hash,
            uint8_t vm_type,
            uint8_t vm_version,
            eosio::chain::apply_context& context
        ) {
            eosio::name receiver = context.get_receiver();
            auto act = context.get_action();

            if (receiver == eosio::name("eosio") &&
                act.name == eosio::name("setcode")) {
                auto setcode_act = act.data_as<chain::setcode>();
                auto trgt_name_it = target_names.find(setcode_act.account);
                if (trgt_name_it != target_names.end()) {
                    // if this setcode action involves an enabled subst
                    // delete subst metadata so that it gets redone, to
                    // fix the case where we deploy a contract with
                    // same hash multiple times

                    ilog("setcode to ${acc} detected...", ("acc", setcode_act.account));

                    fc::sha256 new_code_hash = fc::sha256::hash(
                        setcode_act.code.data(), (uint32_t)setcode_act.code.size() );

                    auto hash_it = enabled_substitutions.find(new_code_hash);
                    if (hash_it != enabled_substitutions.end()) {
                        enabled_substitutions.erase(hash_it);
                        ilog(
                            "cleared old subst metadata for ${hash}",
                            ("hash", new_code_hash)
                        );
                    }
                }
            }

            auto it = enabled_substitutions.find(code_hash);
            if (it != enabled_substitutions.end())
                return false;

            try {
                auto block_num = context.control.pending_block_num();

                // match by name
                auto name_hash = fc::sha256::hash(receiver.to_string());
                auto it = substitutions.find(name_hash);
                if (it != substitutions.end()) {
                    // check if substitution has a from block entry
                    if (auto bnum_it = sub_from.find(name_hash); bnum_it != sub_from.end()) {
                        if (block_num >= bnum_it->second) {
                            perform_replacement(
                                code_hash, it->second, vm_type, vm_version, context);
                        }
                    } else {
                        perform_replacement(
                            code_hash, it->second, vm_type, vm_version, context);
                    }
                }

                // match by hash
                if (auto it = substitutions.find(code_hash); it != substitutions.end()) {
                    // check if substitution has a from block entry
                    if (auto bnum_it = sub_from.find(code_hash); bnum_it != sub_from.end()) {
                        if (block_num >= bnum_it->second) {
                            perform_replacement(
                                code_hash, it->second, vm_type, vm_version, context);
                        }
                    } else {
                        perform_replacement(
                            code_hash, it->second, vm_type, vm_version, context);
                    }
                }

                // no matches for this call
                return false;
            } FC_LOG_AND_RETHROW()
        }

        void register_substitution(
            std::string subst_info,
            std::vector<uint8_t> code
        ) {
            std::vector<std::string> v;
            boost::split(v, subst_info, boost::is_any_of("-"));

            auto from_block = 0;

            if (v.size() == 2) {
                subst_info = v[0];
                from_block = std::stoul(v[1]);
            }

            // store code in internal store
            auto new_hash = fc::sha256::hash((const char*)code.data(), code.size());
            codes[new_hash] = code;

            fc::sha256 info_hash;

            // update substitution maps
            if (subst_info.size() < 16) {
                // if smaller than 16 char assume its an account name
                auto account_name = eosio::name(subst_info);
                info_hash = fc::sha256::hash(account_name.to_string());
            } else {
                // if not assume its a code hash
                info_hash = fc::sha256(subst_info);
            }
            substitutions[info_hash] = new_hash;

            if (from_block > 0)
                sub_from[info_hash] = from_block;

        }

        void load_remote_manifest(std::string chain_id, fc::url manifest_url) {
            string upath = manifest_url.path()->generic_string();

            if (!boost::algorithm::ends_with(upath, "subst.json"))
                wlog("looks like provided url based substitution manifest"
                        "doesn\'t end with \"susbt.json\"... trying anyways...");

            fc::variant manifest = httpc.get_sync_json(manifest_url);
            auto& manif_obj = manifest.get_object();

            ilog("got manifest from ${url}", ("url", manifest_url));

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

                    ilog("downloading wasm from ${wurl}...", ("wurl", wasm_url));
                    std::vector<uint8_t> new_code = httpc.get_sync_raw(wasm_url);
                    ilog("done.");

                    std::string subst_info = subst_entry.key();
                    register_substitution(subst_info, new_code);
                }
            } else {
                ilog("manifest found but chain id not present.");
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
            "subst-by-name", bpo::value<vector<string>>()->composing(),
            "contract_name:new_contract.wasm. Whenever the contract deployed at \"contract_name\""
            "needs to run, substitute debug.wasm in "
            "its place and enable debugging support. This bypasses size limits, timer limits, and "
            "other constraints on debug.wasm. nodeos still enforces constraints on contract.wasm. "
            "(may specify multiple times)");
        options(
            "subst-by-hash", bpo::value<vector<string>>()->composing(),
            "contract_hash:new_contract.wasm. Whenever the contract with \"contract_hash\""
            "needs to run, substitute debug.wasm in "
            "its place and enable debugging support. This bypasses size limits, timer limits, and "
            "other constraints on debug.wasm. nodeos still enforces constraints on contract.wasm. "
            "(may specify multiple times)");
        options(
            "subst-manifest", bpo::value<vector<string>>()->composing(),
            "url. load susbtitution information from a remote json file.");
    }

    void subst_plugin::plugin_initialize(const variables_map& options) {
        auto* chain_plug = app().find_plugin<chain_plugin>();
        auto& control = chain_plug->chain();

        try {
            control.get_wasm_interface().substitute_apply = [this](
                const eosio::chain::digest_type& code_hash,
                uint8_t vm_type, uint8_t vm_version,
                eosio::chain::apply_context& context
            ) {
                return this->my->substitute_apply(code_hash, vm_type, vm_version, context);
            };

            my->db = &control.mutable_db();

            my->app_options = options;

            ilog("installed substitution hook");

            std::string chain_id = control.get_chain_id();

            if (my->app_options.count("subst-by-name")) {
                auto substs = my->app_options.at("subst-by-name").as<vector<string>>();
                for (auto& s : substs) {
                    std::vector<std::string> v;
                    boost::split(v, s, boost::is_any_of(":"));

                    EOS_ASSERT(
                        v.size() == 2,
                        fc::invalid_arg_exception,
                        "Invalid value ${s} for --subst-by-name"
                        " format is {account_name}:{path_to_wasm}", ("s", s)
                    );

                    auto account_name = v[0];
                    auto new_code_path = v[1];

                    std::vector<uint8_t> new_code = eosio::vm::read_wasm(new_code_path);
                    my->register_substitution(account_name, new_code);
                }
            }
            if (my->app_options.count("subst-by-hash")) {
                auto substs = my->app_options.at("subst-by-hash").as<vector<string>>();
                for (auto& s : substs) {
                    std::vector<std::string> v;
                    boost::split(v, s, boost::is_any_of(":"));

                    EOS_ASSERT(
                        v.size() == 2,
                        fc::invalid_arg_exception,
                        "Invalid value ${s} for --subst-by-hash"
                        " format is {contract_hash}:{path_to_wasm}", ("s", s)
                    );

                    auto contract_hash = v[0];
                    auto new_code_path = v[1];

                    std::vector<uint8_t> new_code = eosio::vm::read_wasm(new_code_path);
                    my->register_substitution(contract_hash, new_code);
                }
            }
            if (my->app_options.count("subst-manifest")) {
                auto substs = my->app_options.at("subst-manifest").as<vector<string>>();
                for (auto& s : substs) {
                    auto manifest_url = fc::url(s);
                    EOS_ASSERT(
                        manifest_url.proto() == "http",
                        fc::invalid_arg_exception,
                        "Only http protocol supported for now."
                    );
                    my->load_remote_manifest(chain_id, manifest_url);
                }
            }

            my->debug_print_maps();

        } FC_LOG_AND_RETHROW()
    }  // subst_plugin::plugin_initialize

    void subst_plugin::plugin_startup() {}

    void subst_plugin::plugin_shutdown() {}

}  // namespace eosio
