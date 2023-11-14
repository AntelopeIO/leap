#include <iostream>

#include <boost/program_options.hpp>

#include <benchmark.hpp>

namespace bpo = boost::program_options;
using bpo::options_description;
using bpo::variables_map;

int main(int argc, char* argv[]) {
   uint32_t num_runs = 1;
   std::string feature_name;

   auto features = eosio::benchmark::get_features();

   options_description cli ("benchmark command line options");
   cli.add_options()
      ("feature,f", bpo::value<std::string>(), "feature to be benchmarked; if this option is not present, all features are benchmarked.")
      ("list,l", "list of supported features")
      ("runs,r", bpo::value<uint32_t>(&num_runs)->default_value(1000), "the number of times running a function during benchmarking")
      ("help,h", "benchmark functions, and report average, minimum, and maximum execution time in nanoseconds");

   variables_map vmap;
   try {
      bpo::store(bpo::parse_command_line(argc, argv, cli), vmap);
      bpo::notify(vmap);

      if (vmap.count("help") > 0) {
         cli.print(std::cerr);
         return 0;
      }

      if (vmap.count("list") > 0) {
         auto first = true;
         std::cout << "Supported features are ";
         for (auto& [name, f]: features) {
            if (first) {
               first = false;
            } else {
               std::cout << ", ";
            }
            std::cout << name;
         }
         std::cout << std::endl;
         return 0;
      }

      if (vmap.count("feature") > 0) {
         feature_name = vmap["feature"].as<std::string>();
         if (features.find(feature_name) == features.end()) {
            std::cout << feature_name << " is not supported" << std::endl;
            return 1;
         }
      }
   } catch (bpo::unknown_option &ex) {
      std::cerr << ex.what() << std::endl;
      cli.print (std::cerr);
      return 1;
   } catch( ... ) {
      std::cerr << "unknown exception" << std::endl;
   }

   eosio::benchmark::set_num_runs(num_runs);
   eosio::benchmark::print_header();

   if (feature_name.empty()) {
      for (auto& [name, f]: features) {
         std::cout << name << ":" << std::endl;
         f();
         std::cout << std::endl;
      }
   } else {
      std::cout << feature_name << ":" << std::endl;
      features[feature_name]();
      std::cout << std::endl;
   }

   return 0;
}
