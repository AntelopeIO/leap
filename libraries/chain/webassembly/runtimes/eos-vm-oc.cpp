#include <eosio/chain/webassembly/eos-vm-oc.hpp>
#include <eosio/chain/wasm_eosio_constraints.hpp>
#include <eosio/chain/wasm_eosio_injection.hpp>
#include <eosio/chain/apply_context.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/global_property_object.hpp>

#include <vector>
#include <iterator>

namespace eosio { namespace chain { namespace webassembly { namespace eosvmoc {

class eosvmoc_instantiated_module : public wasm_instantiated_module_interface {
   public:
      eosvmoc_instantiated_module(const digest_type& code_hash, const uint8_t& vm_version, eosvmoc_runtime& wr) :
         _code_hash(code_hash),
         _vm_version(vm_version),
         _eosvmoc_runtime(wr),
         _main_thread_id(std::this_thread::get_id())
      {

      }

      ~eosvmoc_instantiated_module() {
         _eosvmoc_runtime.cc.free_code(_code_hash, _vm_version);
      }

      bool is_main_thread() { return _main_thread_id == std::this_thread::get_id(); };

      void apply(apply_context& context) override {
         const code_descriptor* const cd = _eosvmoc_runtime.cc.get_descriptor_for_code_sync(_code_hash, _vm_version, context.control.is_write_window());
         EOS_ASSERT(cd, wasm_execution_error, "EOS VM OC instantiation failed");

         if ( is_main_thread() )
            _eosvmoc_runtime.exec.execute(*cd, _eosvmoc_runtime.mem, context);
         else
            _eosvmoc_runtime.exec_thread_local->execute(*cd, _eosvmoc_runtime.mem_thread_local, context);
      }

      const digest_type              _code_hash;
      const uint8_t                  _vm_version;
      eosvmoc_runtime&               _eosvmoc_runtime;
      std::thread::id                _main_thread_id;
};

eosvmoc_runtime::eosvmoc_runtime(const boost::filesystem::path data_dir, const eosvmoc::config& eosvmoc_config, const chainbase::database& db)
   : cc(data_dir, eosvmoc_config, db), exec(cc), mem(wasm_constraints::maximum_linear_memory/wasm_constraints::wasm_page_size) {
}

eosvmoc_runtime::~eosvmoc_runtime() {
}

std::unique_ptr<wasm_instantiated_module_interface> eosvmoc_runtime::instantiate_module(const char* code_bytes, size_t code_size, std::vector<uint8_t> initial_memory,
                                                                                        const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version) {
   return std::make_unique<eosvmoc_instantiated_module>(code_hash, vm_type, *this);
}

//never called. EOS VM OC overrides eosio_exit to its own implementation
void eosvmoc_runtime::immediately_exit_currently_running_module() {}

void eosvmoc_runtime::init_thread_local_data() {
   exec_thread_local = std::make_unique<eosvmoc::executor>(cc);
}

thread_local std::unique_ptr<eosvmoc::executor> eosvmoc_runtime::exec_thread_local {};
thread_local eosvmoc::memory eosvmoc_runtime::mem_thread_local{ wasm_constraints::maximum_linear_memory/wasm_constraints::wasm_page_size };

}}}}
