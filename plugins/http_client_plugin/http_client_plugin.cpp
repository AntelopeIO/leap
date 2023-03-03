#include <eosio/http_client_plugin/http_client_plugin.hpp>
#include <eosio/chain/exceptions.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <fstream>

namespace eosio {

http_client_plugin::http_client_plugin():my(new http_client()){}
http_client_plugin::~http_client_plugin(){}

void http_client_plugin::set_program_options(options_description&, options_description& cfg) {
}

void http_client_plugin::plugin_initialize(const variables_map& options) {
}

void http_client_plugin::plugin_startup() {

}

void http_client_plugin::plugin_shutdown() {

}

}
