#pragma once

#include <boost/beast/http.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

#include <debug_eos_vm/debug_contract.hpp>

#include <fc/io/json.hpp>
#include <fc/network/url.hpp>
#include <fc/network/http/http_client.hpp>

#include <eosio/chain/controller.hpp>
#include <eosio/chain/application.hpp>
#include <eosio/chain/transaction_context.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>

namespace http = boost::beast::http;

namespace eosio
{
   using chain::controller;
   typedef boost::filesystem::path bpath;

   struct subst_plugin_impl;

   class subst_plugin : public appbase::plugin<subst_plugin>
   {
     public:
      APPBASE_PLUGIN_REQUIRES((chain_plugin))

      subst_plugin();
      virtual ~subst_plugin();

      void set_program_options(appbase::options_description& cli,
                               appbase::options_description& cfg) override;
      void plugin_initialize(const appbase::variables_map& options);
      void plugin_startup();
      void plugin_shutdown();

     private:
      std::shared_ptr<subst_plugin_impl> my;
   };
}  // namespace eosio
