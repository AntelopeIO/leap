#include "subst_plugin.hpp"

namespace eosio {

    static auto _subst_plugin = application::register_plugin<subst_plugin>();

    using contract_backend = eosio::vm::backend<
        eosio::chain::eos_vm_host_functions_t,
        eosio::vm::jit_profile
    >;

    using wasm_module = std::unique_ptr<contract_backend>;

    struct subst_plugin_impl : std::enable_shared_from_this<subst_plugin_impl> {

        std::map<fc::sha256, fc::sha256> substitutions;
        std::map<fc::sha256, uint32_t> sub_from;

        std::map<fc::sha256, std::vector<uint8_t>> codes;
        std::map<fc::sha256, wasm_module> cached_modules;

        fc::http_client httpc;

        void debug_print_maps() {
            // print susbtitution maps for debug
            ilog("Loaded substitutions:");
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

        wasm_module& get_module(const eosio::chain::digest_type& code_hash) {
            if (auto it = cached_modules.find(code_hash); it != cached_modules.end())
                return it->second;

            if (auto it = codes.find(code_hash); it != codes.end()) {
                try {
                    eosio::vm::wasm_code_ptr code(it->second.data(), it->second.size());
                    auto bkend = std::make_unique<contract_backend>(code, it->second.size(), nullptr);
                    eosio::chain::eos_vm_host_functions_t::resolve(bkend->get_module());
                    return cached_modules[code_hash] = std::move(bkend);
                } catch (eosio::vm::exception& e) {
                    FC_THROW_EXCEPTION(eosio::chain::wasm_execution_error,
                        "Error building eos-vm interp: ${e}", ("e", e.what()));
                }
            }
            throw std::runtime_error{"missing code for substituted module"};
        }  // get_module

        void perform_call(fc::sha256 hsum, eosio::chain::apply_context& context) {
            auto& module = *get_module(hsum);
            module.set_wasm_allocator(&context.control.get_wasm_allocator());
            eosio::chain::webassembly::interface iface(context);
            module.initialize(&iface);
            module.call(iface, "env", "apply", context.get_receiver().to_uint64_t(),
                        context.get_action().account.to_uint64_t(),
                        context.get_action().name.to_uint64_t());
        }

        bool substitute_apply(
            const fc::sha256& code_hash,
            uint8_t vm_type,
            uint8_t vm_version,
            eosio::chain::apply_context& context
        ) {
            if (vm_type || vm_version)
                return false;

            auto block_num = context.control.pending_block_num();

            // match by name
            auto name_hash = fc::sha256::hash(context.get_receiver().to_string());
            auto it = substitutions.find(name_hash);
            if (it != substitutions.end()) {
                if (auto bnum_it = sub_from.find(name_hash); bnum_it != sub_from.end()) {
                    if (block_num >= bnum_it->second) {
                        perform_call(it->second, context);
                        return true;
                    }
                } else {
                    perform_call(it->second, context);
                    return true;
                }
            }

            // match by hash
            if (auto it = substitutions.find(code_hash); it != substitutions.end()) {
                if (auto bnum_it = sub_from.find(code_hash); bnum_it != sub_from.end()) {
                    if (block_num >= bnum_it->second) {
                        perform_call(it->second, context);
                        return true;
                    }
                } else {
                    perform_call(it->second, context);
                    return true;
                }
            }

            // no matches for this call
            return false;
        }

        fc::sha256 store_code(std::vector<uint8_t> new_code) {
            auto new_hash = fc::sha256::hash((const char*)new_code.data(), new_code.size());
            codes[new_hash] = std::move(new_code);
            return new_hash;
        }

        void subst(const fc::sha256 old_hash, std::vector<uint8_t> new_code, uint32_t from_block) {
            auto new_hash = store_code(new_code);
            substitutions[old_hash] = new_hash;
            if (from_block > 0)
                sub_from[old_hash] = from_block;
        }

        void subst(const eosio::name account_name, std::vector<uint8_t> new_code, uint32_t from_block) {
            auto new_hash = store_code(new_code);
            auto acc_hash = fc::sha256::hash(account_name.to_string());
            substitutions[acc_hash] = new_hash;
            if (from_block > 0)
                sub_from[acc_hash] = from_block;
        }

        void subst(std::string& subst_info, std::vector<uint8_t> new_code) {
            std::vector<std::string> v;
            boost::split(v, subst_info, boost::is_any_of("-"));

            auto from_block = 0;

            if (v.size() == 2) {
                subst_info = v[0];
                from_block = std::stoul(v[1]);
            }

            // update substitution maps
            if (subst_info.size() < 16) {
                // if smaller than 16 char assume its an account name
                subst(eosio::name(subst_info), new_code, from_block);
            } else {
                // if not assume its a code hash
                subst(fc::sha256(subst_info), new_code, from_block);
            }

        }

        void load_remote_manifest(std::string chain_id, fc::url manifest_url) {
            string upath = manifest_url.path()->generic_string();

            if (!boost::algorithm::ends_with(upath, "subst.json"))
                wlog("Looks like provided url based substitution manifest"
                        "doesn\'t end with \"susbt.json\"... trying anyways...");

            fc::variant manifest = httpc.get_sync_json(manifest_url);
            auto& manif_obj = manifest.get_object();

            ilog("Got manifest from ${url}", ("url", manifest_url));

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

                    ilog("Downloading wasm from ${wurl}...", ("wurl", wasm_url));
                    std::vector<uint8_t> new_code = httpc.get_sync_raw(wasm_url);
                    ilog("Done.");

                    std::string subst_info = subst_entry.key();
                    subst(subst_info, new_code);
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
        std::string chain_id = control.get_chain_id();
        try {
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
                    my->subst(account_name, new_code);
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
                    my->subst(contract_hash, new_code);
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
                    my->load_remote_manifest(chain_id, manifest_url);
                }
            }

            my->debug_print_maps();

            auto& iface = control.get_wasm_interface();
            iface.substitute_apply = [this](
                const eosio::chain::digest_type& code_hash,
                uint8_t vm_type, uint8_t vm_version,
                eosio::chain::apply_context& context
            ) {
                return this->my->substitute_apply(code_hash, vm_type, vm_version, context);
            };
        }
        FC_LOG_AND_RETHROW()
    }  // subst_plugin::plugin_initialize

    void subst_plugin::plugin_startup() {}

    void subst_plugin::plugin_shutdown() {}

}  // namespace eosio
