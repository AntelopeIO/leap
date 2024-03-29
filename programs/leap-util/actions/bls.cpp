#include "bls.hpp"

#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/bls_signature.hpp>

#include <boost/program_options.hpp>

using namespace fc::crypto::blslib;
namespace bpo = boost::program_options;
using bpo::options_description;

void bls_actions::setup(CLI::App& app) {
   // callback helper with error code handling
   auto err_guard = [this](int (bls_actions::*fun)()) {
      try {
         int rc = (this->*fun)();
         if(rc) throw(CLI::RuntimeError(rc));
      } catch(...) {
         print_exception();
         throw(CLI::RuntimeError(-1));
      }
   };

   // main command
   auto* sub = app.add_subcommand("bls", "BLS utility");
   sub->require_subcommand();

   // Create subcommand
   auto create = sub->add_subcommand("create", "Create BLS items");
   create->require_subcommand();

   // sub-subcommand - key 
   auto* create_key = create->add_subcommand("key", "Create a new BLS keypair and print the public and private keys")->callback([err_guard]() { err_guard(&bls_actions::create_key); });
   create_key->add_option("-f,--file", opt->key_file, "Name of file to write private/public key output to. (Must be set, unless \"--to-console\" is passed");
   create_key->add_flag( "--to-console", opt->print_console, "Print private/public keys to console.");

   // sub-subcommand - pop (proof of possession) 
   auto* create_pop = create->add_subcommand("pop", "Create proof of possession of the corresponding private key for a given public key")->callback([err_guard]() { err_guard(&bls_actions::create_pop); });
   create_pop->add_option("-f,--file", opt->key_file, "Name of file storing the private key. (one and only one of \"-f,--file\" and \"--private-key\" must be set)");
   create_pop->add_option("--private-key", opt->private_key_str, "The private key. (one and only one of \"-f,--file\" and \"--private-key\" must be set)");
}

int bls_actions::create_key() {
   if (opt->key_file.empty() && !opt->print_console) {
      std::cerr << "ERROR: Either indicate a file using \"-f, --file\" or pass \"--to-console\"" << "\n";
      return -1;
   } else if (!opt->key_file.empty() && opt->print_console) {
      std::cerr << "ERROR: Only one of \"-f, --file\" or pass \"--to-console\" can be provided" << "\n";
      return -1;
   }

   // create a private key and get its corresponding public key
   const bls_private_key private_key = bls_private_key::generate();
   const bls_public_key public_key = private_key.get_public_key();

   // generate proof of possession
   const bls_signature pop = private_key.proof_of_possession();

   // prepare output
   std::string out_str = "Private key: " + private_key.to_string() + "\n";
   out_str += "Public key: " + public_key.to_string() + "\n";
   out_str += "Proof of Possession: " + pop.to_string() + "\n";
   if (opt->print_console) {
      std::cout << out_str;
   } else {
      std::cout << "saving keys to " << opt->key_file << "\n";
      std::ofstream out( opt->key_file.c_str() );
      out << out_str;
   }

   return 0;
}

int bls_actions::create_pop() {
   if (opt->key_file.empty() && opt->private_key_str.empty()) {
      std::cerr << "ERROR: Either indicate a file using \"-f, --file\" or pass \"--private-key\"" << "\n";
      return -1;
   } else if (!opt->key_file.empty() && !opt->private_key_str.empty()) {
      std::cerr << "ERROR: Only one of \"-f, --file\" and \"--private-key\" can be provided" << "\n";
      return -1;
   }

   std::string private_key_str;
   if (!opt->private_key_str.empty()) {
      private_key_str = opt->private_key_str;
   } else {
      std::ifstream key_file(opt->key_file);

      if (!key_file.is_open()) {
         std::cerr << "ERROR: failed to open file " << opt->key_file << "\n";
         return -1;
      }

      if (std::getline(key_file, private_key_str)) {
         if (!key_file.eof()) {
            std::cerr << "ERROR: file " << opt->key_file << " contains more than one line" << "\n";
            return -1;
         }
      } else {
         std::cerr << "ERROR: file " << opt->key_file << " is empty" << "\n";
         return -1;
      }
   }

   // create private key object using input private key string
   const bls_private_key private_key = bls_private_key(private_key_str);
   const bls_public_key public_key = private_key.get_public_key();
   const bls_signature pop = private_key.proof_of_possession();

   std::cout << "Proof of Possession: " << pop.to_string()<< "\n";
   std::cout << "Public key: " <<  public_key.to_string() << "\n";

   return 0;
}
