#include <chrono>
#include <iostream>
#include <iomanip>
#include <locale>

#include <benchmark.hpp>

namespace eosio::benchmark {

// update this map when a new feature is supported
// key is the name and value is the function doing benchmarking
std::map<std::string, std::function<void()>> features {
   { "alt_bn_128", alt_bn_128_benchmarking },
   { "modexp", modexp_benchmarking },
   { "key", key_benchmarking },
   { "hash", hash_benchmarking },
   { "blake2", blake2_benchmarking },
   { "bls", bls_benchmarking },
   { "merkle", merkle_benchmarking }
};

// values to control cout format
constexpr auto name_width = 40;
constexpr auto runs_width = 5;
constexpr auto time_width = 12;
constexpr auto ns_width = 2;

uint32_t num_runs = 1;

std::map<std::string, std::function<void()>> get_features() {
   return features;
}

void set_num_runs(uint32_t runs) {
   num_runs = runs;
}

uint32_t get_num_runs() {
   return num_runs;
}

void print_header() {
   std::cout << std::left << std::setw(name_width) << "function"
      << std::setw(runs_width) << "runs"
      << std::setw(time_width + ns_width) << std::right << "average"
      << std::setw(time_width + ns_width) << "minimum"
      << std::setw(time_width + ns_width) << "maximum"
      << std::endl << std::endl;
}

void print_results(std::string name, uint32_t runs, uint64_t total, uint64_t min, uint64_t max) {
   std::cout.imbue(std::locale(""));
   std::cout
      << std::setw(name_width) << std::left << name
      // std::fixed for not printing 1234 in 1.234e3.
      // setprecision(0) for not printing fractions
      << std::right << std::fixed << std::setprecision(0)
      << std::setw(runs_width)  << runs
      << std::setw(time_width) << total/runs << std::setw(ns_width) << " ns"
      << std::setw(time_width) << min << std::setw(ns_width) << " ns"
      << std::setw(time_width) << max << std::setw(ns_width) << " ns"
      << std::endl;
}

bytes to_bytes(const std::string& source) {
   bytes output(source.length()/2);
   fc::from_hex(source, output.data(), output.size());
   return output;
};

void benchmarking(const std::string& name, const std::function<void()>& func,
                  std::optional<size_t> opt_num_runs /* = {} */) {
   uint64_t total{0};
   uint64_t min{std::numeric_limits<uint64_t>::max()};
   uint64_t max{0};
   uint32_t runs = opt_num_runs ? *opt_num_runs : num_runs;

   for (auto i = 0U; i < runs; ++i) {
      auto start_time = std::chrono::high_resolution_clock::now();
      func();
      auto end_time = std::chrono::high_resolution_clock::now();

      uint64_t duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
      total += duration;
      min = std::min(min, duration);
      max = std::max(max, duration);
   }

   print_results(name, runs, total, min, max);
}

} // benchmark
