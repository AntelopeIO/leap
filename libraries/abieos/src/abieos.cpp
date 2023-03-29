// copyright defined in abieos/LICENSE.txt

#include "eosio/abieos.h"
#include "abieos.hpp"
#include "eosio/hex.hpp"

#include <memory>

inline const bool catch_all = true;

using namespace abieos;

struct abieos_context_s
{
   const char* last_error = "";
   std::string last_error_buffer{};
   std::string result_str{};
   std::vector<char> result_bin{};

   std::map<name, abi> contracts{};
};

static void fix_null_str(const char*& s)
{
   if (!s)
      s = "";
}

static bool set_error(abieos_context* context, std::string error) noexcept
{
   context->last_error_buffer = std::move(error);
   context->last_error = context->last_error_buffer.c_str();
   return false;
}

template <typename T, typename F>
static auto handle_exceptions(abieos_context* context, T errval, F f) noexcept -> decltype(f())
{
   if (!context)
      return errval;
   try
   {
      return f();
   }
   catch (std::exception& e)
   {
      if (!catch_all)
         throw;
      set_error(context, e.what());
      return errval;
   }
   catch (...)
   {
      if (!catch_all)
         throw;
      set_error(context, "unknown exception");
      return errval;
   }
}

extern "C" abieos_context* abieos_create()
{
   try
   {
      return new abieos_context{};
   }
   catch (...)
   {
      if (!catch_all)
         throw;
      return nullptr;
   }
}

extern "C" void abieos_destroy(abieos_context* context)
{
   delete context;
}

extern "C" const char* abieos_get_error(abieos_context* context)
{
   if (!context)
      return "context is null";
   return context->last_error;
}

extern "C" int abieos_get_bin_size(abieos_context* context)
{
   if (!context)
      return 0;
   return context->result_bin.size();
}

extern "C" const char* abieos_get_bin_data(abieos_context* context)
{
   if (!context)
      return nullptr;
   return context->result_bin.data();
}

extern "C" const char* abieos_get_bin_hex(abieos_context* context)
{
   return handle_exceptions(context, nullptr, [&] {
      context->result_str.clear();
      eosio::hex(context->result_bin.begin(), context->result_bin.end(),
                 std::back_inserter(context->result_str));
      return context->result_str.c_str();
   });
}

extern "C" uint64_t abieos_string_to_name(abieos_context* context, const char* str)
{
   fix_null_str(str);
   return eosio::string_to_name(str);
}

extern "C" const char* abieos_name_to_string(abieos_context* context, uint64_t name)
{
   return handle_exceptions(context, nullptr, [&] {
      context->result_str = eosio::name_to_string(name);
      return context->result_str.c_str();
   });
}

extern "C" abieos_bool abieos_set_abi(abieos_context* context, uint64_t contract, const char* abi)
{
   fix_null_str(abi);
   return handle_exceptions(context, false, [&]() {
      context->last_error = "abi parse error";
      abi_def def{};
      std::string error;
      std::string abi_copy{abi};
      eosio::json_token_stream stream(abi_copy.data());
      from_json(def, stream);
      if (!eosio::check_abi_version(def.version, error))
         return set_error(context, std::move(error));
      abieos::abi c;
      convert(def, c);
      context->contracts.insert({name{contract}, std::move(c)});
      return true;
   });
}

extern "C" abieos_bool abieos_set_abi_bin(abieos_context* context,
                                          uint64_t contract,
                                          const char* data,
                                          size_t size)
{
   return handle_exceptions(context, false, [&] {
      context->last_error = "abi parse error";
      if (!data || !size)
         return set_error(context, "no data");
      std::string error;
      eosio::input_stream stream{data, size};
      std::string version;
      from_bin(version, stream);
      if (!eosio::check_abi_version(version, error))
         return set_error(context, std::move(error));
      abi_def def{};
      stream = {data, size};
      from_bin(def, stream);
      abieos::abi c;
      convert(def, c);
      context->contracts.insert({name{contract}, std::move(c)});
      return true;
   });
}

extern "C" abieos_bool abieos_set_abi_hex(abieos_context* context,
                                          uint64_t contract,
                                          const char* hex)
{
   fix_null_str(hex);
   return handle_exceptions(context, false, [&]() -> abieos_bool {
      std::vector<char> data;
      std::string error;
      if (!unhex(error, hex, hex + strlen(hex), std::back_inserter(data)))
      {
         if (!error.empty())
            set_error(context, std::move(error));
         return false;
      }
      return abieos_set_abi_bin(context, contract, data.data(), data.size());
   });
}

extern "C" const char* abieos_get_type_for_action(abieos_context* context,
                                                  uint64_t contract,
                                                  uint64_t action)
{
   return handle_exceptions(context, nullptr, [&] {
      auto contract_it = context->contracts.find(::abieos::name{contract});
      if (contract_it == context->contracts.end())
         throw std::runtime_error("contract \"" + eosio::name_to_string(contract) +
                                  "\" is not loaded");
      auto& c = contract_it->second;

      auto action_it = c.action_types.find(name{action});
      if (action_it == c.action_types.end())
         throw std::runtime_error("contract \"" + eosio::name_to_string(contract) +
                                  "\" does not have action \"" + eosio::name_to_string(action) +
                                  "\"");
      return action_it->second.c_str();
   });
}

extern "C" const char* abieos_get_type_for_table(abieos_context* context,
                                                 uint64_t contract,
                                                 uint64_t table)
{
   return handle_exceptions(context, nullptr, [&] {
      auto contract_it = context->contracts.find(::abieos::name{contract});
      if (contract_it == context->contracts.end())
         throw std::runtime_error("contract \"" + eosio::name_to_string(contract) +
                                  "\" is not loaded");
      auto& c = contract_it->second;

      auto table_it = c.table_types.find(name{table});
      if (table_it == c.table_types.end())
         throw std::runtime_error("contract \"" + eosio::name_to_string(contract) +
                                  "\" does not have table \"" + eosio::name_to_string(table) +
                                  "\"");
      return table_it->second.c_str();
   });
}

extern "C" abieos_bool abieos_json_to_bin(abieos_context* context,
                                          uint64_t contract,
                                          const char* type,
                                          const char* json)
{
   fix_null_str(type);
   fix_null_str(json);
   return handle_exceptions(context, false, [&] {
      context->last_error = "json parse error";
      auto contract_it = context->contracts.find(::abieos::name{contract});
      if (contract_it == context->contracts.end())
         return set_error(context,
                          "contract \"" + eosio::name_to_string(contract) + "\" is not loaded");
      std::string error;
      auto t = contract_it->second.get_type(type);
      context->result_bin.clear();
      context->result_bin = t->json_to_bin(json);
      return true;
   });
}

extern "C" abieos_bool abieos_json_to_bin_reorderable(abieos_context* context,
                                                      uint64_t contract,
                                                      const char* type,
                                                      const char* json)
{
   fix_null_str(type);
   fix_null_str(json);
   return handle_exceptions(context, false, [&] {
      context->last_error = "json parse error";
      auto contract_it = context->contracts.find(::abieos::name{contract});
      if (contract_it == context->contracts.end())
         return set_error(context,
                          "contract \"" + eosio::name_to_string(contract) + "\" is not loaded");
      std::string error;
      auto t = contract_it->second.get_type(type);
      context->result_bin.clear();
      context->result_bin = t->json_to_bin_reorderable(json);
      return true;
   });
}

extern "C" const char* abieos_bin_to_json(abieos_context* context,
                                          uint64_t contract,
                                          const char* type,
                                          const char* data,
                                          size_t size)
{
   fix_null_str(type);
   return handle_exceptions(context, nullptr, [&]() -> const char* {
      if (!data)
         size = 0;
      context->last_error = "binary decode error";
      auto contract_it = context->contracts.find(::abieos::name{contract});
      std::string error;
      if (contract_it == context->contracts.end())
      {
         (void)set_error(error,
                         "contract \"" + eosio::name_to_string(contract) + "\" is not loaded");
         return nullptr;
      }
      auto t = contract_it->second.get_type(type);
      eosio::input_stream bin{data, size};
      context->result_str = t->bin_to_json(bin);
      if (bin.pos != bin.end)
         throw std::runtime_error("Extra data");
      return context->result_str.c_str();
   });
}

extern "C" const char* abieos_hex_to_json(abieos_context* context,
                                          uint64_t contract,
                                          const char* type,
                                          const char* hex)
{
   fix_null_str(hex);
   return handle_exceptions(context, nullptr, [&]() -> const char* {
      std::vector<char> data;
      std::string error;
      if (!unhex(error, hex, hex + strlen(hex), std::back_inserter(data)))
      {
         if (!error.empty())
            set_error(context, std::move(error));
         return nullptr;
      }
      return abieos_bin_to_json(context, contract, type, data.data(), data.size());
   });
}
