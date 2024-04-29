#pragma once

#include <functional>
#include <map>
#include <vector>
#include <limits>
#include <optional>

#include <fc/crypto/hex.hpp>

namespace eosio::benchmark {
using bytes = std::vector<char>;

void set_num_runs(uint32_t runs);
uint32_t get_num_runs();
std::map<std::string, std::function<void()>> get_features();
void print_header();
bytes to_bytes(const std::string& source);

void alt_bn_128_benchmarking();
void modexp_benchmarking();
void key_benchmarking();
void hash_benchmarking();
void blake2_benchmarking();
void bls_benchmarking();
void merkle_benchmarking();

void benchmarking(const std::string& name, const std::function<void()>& func, std::optional<size_t> num_runs = {});

} // benchmark
