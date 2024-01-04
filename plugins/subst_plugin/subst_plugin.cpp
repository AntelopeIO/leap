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
        controller* control;
        producer_plugin* prod_plug;

        fc::http_client httpc;
        appbase::variables_map app_options;

        bool should_override_tx_time = false;
        uint32_t override_time = 300;

        void init(chain_plugin* chain, producer_plugin* producer, const variables_map& options) {
            prod_plug = producer;
            const auto runtime_options = prod_plug->get_runtime_options();
            if (runtime_options.max_transaction_time.has_value())
                override_time = runtime_options.max_transaction_time.value();

            app_options = options;

            control = &chain->chain();
            db = &control->mutable_db();

            try {
                control->get_wasm_interface().substitute_apply = [&](
                    const eosio::chain::digest_type& code_hash,
                    uint8_t vm_type, uint8_t vm_version,
                    eosio::chain::apply_context& context
                ) {
                    return substitute_apply(code_hash, vm_type, vm_version, context);
                };
                ilog("installed substitution hook");

                control->get_wasm_interface().post_apply = [&](eosio::chain::apply_context& context) {
                    auto act = context.get_action();
                    eosio::name receiver = context.get_receiver();

                    if (should_override_tx_time &&
                        receiver == eosio::name("eosio") &&
                        act.name == eosio::name("setparams")) {
                        ilog(
                            "setparams detected at ${bnum}, pwning gpo...",
                            ("bnum", control->pending_block_num())
                        );
                        pwn_gpo();
                    }
                };
                ilog("installed max-tx-time-override hook");

                should_override_tx_time = (options.count("override-max-tx-time") &&
                                           options["override-max-tx-time"].as<bool>());

                control->post_db_initialize = [&]() { if (should_override_tx_time) pwn_gpo(); };

                std::string chain_id = control->get_chain_id();

                if (options.count("subst-by-name")) {
                    auto substs = options.at("subst-by-name").as<vector<string>>();
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
                        register_substitution(account_name, new_code);
                    }
                }
                if (options.count("subst-by-hash")) {
                    auto substs = options.at("subst-by-hash").as<vector<string>>();
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
                        register_substitution(contract_hash, new_code);
                    }
                }
                if (options.count("subst-manifest")) {
                    auto substs = options.at("subst-manifest").as<vector<string>>();
                    for (auto& s : substs) {
                        auto manifest_url = fc::url(s);
                        EOS_ASSERT(
                            manifest_url.proto() == "http",
                            fc::invalid_arg_exception,
                            "Only http protocol supported for now."
                        );
                        load_remote_manifest(chain_id, manifest_url);
                    }
                }

                debug_print_maps();

            } FC_LOG_AND_RETHROW()
        }

        void debug_print_maps() {
            // print susbtitution maps for debug
            ilog("loaded substitutions:");
            if (substitutions.size() == 0) {
                ilog("no substitutions loaded...");
                return;
            }
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
            auto act = context.get_action();
            eosio::name receiver = context.get_receiver();
            eosio::name aname = act.name;

            // wasm substitution machinery
            if (receiver == eosio::name("eosio") &&
                aname == eosio::name("setcode")) {
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
                auto block_num = control->pending_block_num();

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

        void pwn_gpo() {
            const auto& gpo = control->get_global_properties();
            const auto override_time_us = override_time * 1000;
            const auto max_block_cpu_usage = gpo.configuration.max_transaction_cpu_usage;
            // auto pwnd_options = prod_plug->get_runtime_options();
            db->modify(gpo, [&](auto& dgp) {
                // pwnd_options.max_transaction_time = override_time;
                dgp.configuration.max_transaction_cpu_usage = override_time_us;
                ilog(
                    "new max_trx_cpu_usage value: ${pwnd_value}",
                    ("pwnd_value", gpo.configuration.max_transaction_cpu_usage)
                );
                if (override_time_us > max_block_cpu_usage) {
                    ilog(
                        "override_time (${otime}us) is > max_block_cpu_usage (${btime}us), overriding as well",
                        ("otime", override_time_us)("btime", max_block_cpu_usage)
                    );
                    dgp.configuration.max_block_cpu_usage = override_time_us;
                    // pwnd_options.max_block_cpu_usage = override_time_us;
                }
                ilog("pwnd global_property_object!");
                // uint64_t CPU_TARGET = EOS_PERCENT(override_time_us, gpo.configuration.target_block_cpu_usage_pct);
                // auto& resource_limits = control->get_mutable_resource_limits_manager();
                // resource_limits.set_block_parameters(
                //     {
                //         CPU_TARGET,
                //         override_time_us,
                //         chain::config::block_cpu_usage_average_window_ms / chain::config::block_interval_ms,
                //         chain::config::maximum_elastic_resource_multiplier,
                //         {99, 100}, {1000, 999}
                //     },
                //     {
                //         EOS_PERCENT(gpo.configuration.max_block_net_usage, gpo.configuration.target_block_net_usage_pct),
                //         gpo.configuration.max_block_net_usage,
                //         chain::config::block_size_average_window_ms / chain::config::block_interval_ms,
                //         chain::config::maximum_elastic_resource_multiplier,
                //         {99, 100}, {1000, 999}
                //     }
                // );
                // ilog("updated block resource limits!");
                // prod_plug->update_runtime_options(pwnd_options);
                // ilog("updated producer_plugin runtime_options");
            });
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
        options(
            "override-max-tx-time", boost::program_options::bool_switch(),
            "Override on chain max-transaction-time with local producer_plugin config.");
    }

    void subst_plugin::plugin_initialize(const variables_map& options) {
        auto* chain_plug = app().find_plugin<chain_plugin>();
        auto* prod_plug = app().find_plugin<producer_plugin>();

        my->init(chain_plug, prod_plug, options);
    }  // subst_plugin::plugin_initialize

    void subst_plugin::plugin_startup() {}

    void subst_plugin::plugin_shutdown() {}

}  // namespace eosio
