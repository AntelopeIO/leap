#include "bls_primitives_test.hpp"
#include <eosio/transaction.hpp>

using namespace eosio;

void bls_primitives_test::testg1add(const std::vector<uint8_t>& op1, const std::vector<uint8_t>& op2, const std::vector<uint8_t>& res, int32_t expected_error)
{
    bls_g1 r{};
    int32_t error = internal_use_do_not_use::bls_g1_add(
        reinterpret_cast<const char*>(op1.data()),
        sizeof(bls_g1),
        reinterpret_cast<const char*>(op2.data()),
        sizeof(bls_g1),
        reinterpret_cast<char*>(r),
        sizeof(bls_g1)
    );
    check(error == expected_error, "bls_g1_add: Error does not match");
    check(0 == std::memcmp(r, res.data(), sizeof(bls_g1)), "bls_g1_add: Result does not match");
}

void bls_primitives_test::testg2add(const std::vector<uint8_t>& op1, const std::vector<uint8_t>& op2, const std::vector<uint8_t>& res, int32_t expected_error)
{
    bls_g2 r{};
    int32_t error = internal_use_do_not_use::bls_g2_add(
        reinterpret_cast<const char*>(op1.data()),
        sizeof(bls_g2),
        reinterpret_cast<const char*>(op2.data()),
        sizeof(bls_g2),
        reinterpret_cast<char*>(r),
        sizeof(bls_g2)
    );
    check(error == expected_error, "bls_g2_add: Error does not match");
    check(0 == std::memcmp(r, res.data(), sizeof(bls_g2)), "bls_g2_add: Result does not match");
}

void bls_primitives_test::testg1wsum(const std::vector<uint8_t>& points, const std::vector<uint8_t>& scalars, const uint32_t num, const std::vector<uint8_t>& res, int32_t expected_error)
{
    bls_g1 r{};
    int32_t error = internal_use_do_not_use::bls_g1_weighted_sum(
        reinterpret_cast<const char*>(points.data()),
        num * sizeof(bls_g1),
        reinterpret_cast<const char*>(scalars.data()),
        num * sizeof(bls_scalar),
        num,
        reinterpret_cast<char*>(r),
        sizeof(bls_g1)
    );
    check(error == expected_error, "bls_g1_exp: Error does not match");
    check(0 == std::memcmp(r, res.data(), sizeof(bls_g1)), "bls_g1_exp: Result does not match");
}

void bls_primitives_test::testg2wsum(const std::vector<uint8_t>& points, const std::vector<uint8_t>& scalars, const uint32_t num, const std::vector<uint8_t>& res, int32_t expected_error)
{
    bls_g2 r{};
    int32_t error = internal_use_do_not_use::bls_g2_weighted_sum(
        reinterpret_cast<const char*>(points.data()),
        num * sizeof(bls_g2),
        reinterpret_cast<const char*>(scalars.data()),
        num * sizeof(bls_scalar),
        num,
        reinterpret_cast<char*>(r),
        sizeof(bls_g2)
    );
    check(error == expected_error, "bls_g2_exp: Error does not match");
    check(0 == std::memcmp(r, res.data(), sizeof(bls_g2)), "bls_g2_exp: Result does not match");
}

void bls_primitives_test::testpairing(const std::vector<uint8_t>& g1_points, const std::vector<uint8_t>& g2_points, const uint32_t num, const std::vector<uint8_t>& res, int32_t expected_error)
{
    bls_gt r{};
    int32_t error = internal_use_do_not_use::bls_pairing(
        reinterpret_cast<const char*>(g1_points.data()),
        num * sizeof(bls_g1),
        reinterpret_cast<const char*>(g2_points.data()),
        num * sizeof(bls_g2),
        num,
        reinterpret_cast<char*>(r),
        sizeof(bls_gt)
    );
    check(error == expected_error, "bls_pairing: Error does not match");
    check(0 == std::memcmp(r, res.data(), sizeof(bls_gt)), "bls_pairing: Result does not match");
}

void bls_primitives_test::testg1map(const std::vector<uint8_t>& e, const std::vector<uint8_t>& res, int32_t expected_error)
{
    bls_g1 r{};
    int32_t error = internal_use_do_not_use::bls_g1_map(
        reinterpret_cast<const char*>(e.data()),
        sizeof(bls_fp),
        reinterpret_cast<char*>(r),
        sizeof(bls_g1)
    );
    check(error == expected_error, "bls_g1_map: Error does not match");
    check(0 == std::memcmp(r, res.data(), sizeof(bls_g1)), "bls_g1_map: Result does not match");
}

void bls_primitives_test::testg2map(const std::vector<uint8_t>& e, const std::vector<uint8_t>& res, int32_t expected_error)
{
    bls_g2 r{};
    int32_t error = internal_use_do_not_use::bls_g2_map(
        reinterpret_cast<const char*>(e.data()),
        sizeof(bls_fp2),
        reinterpret_cast<char*>(r),
        sizeof(bls_g2)
    );
    check(error == expected_error, "bls_g2_map: Error does not match");
    check(0 == std::memcmp(r, res.data(), sizeof(bls_g2)), "bls_g2_map: Result does not match");
}

void bls_primitives_test::testfpmul(const std::vector<uint8_t>& op1, const std::vector<uint8_t>& op2, const std::vector<uint8_t>& res, int32_t expected_error)
{
    bls_fp r{};
    int32_t error = internal_use_do_not_use::bls_fp_mul(
        reinterpret_cast<const char*>(op1.data()),
        sizeof(bls_fp),
        reinterpret_cast<const char*>(op2.data()),
        sizeof(bls_fp),
        reinterpret_cast<char*>(r),
        sizeof(bls_fp)
    );
    check(error == expected_error, "bls_fp_mul: Error does not match");
    check(0 == std::memcmp(r, res.data(), sizeof(bls_fp)), "bls_fp_mul: Result does not match");
}

void bls_primitives_test::testfpexp(const std::vector<uint8_t>& base, const std::vector<uint8_t>& exp, const std::vector<uint8_t>& res, int32_t expected_error)
{
    bls_fp r{};
    int32_t error = internal_use_do_not_use::bls_fp_exp(
        reinterpret_cast<const char*>(base.data()),
        sizeof(bls_fp),
        reinterpret_cast<const char*>(exp.data()),
        sizeof(bls_scalar_large),
        reinterpret_cast<char*>(r),
        sizeof(bls_fp)
    );
    check(error == expected_error, "bls_fp_exp: Error does not match");
    check(0 == std::memcmp(r, res.data(), sizeof(bls_fp)), "bls_fp_exp: Result does not match");
}


void bls_primitives_test::testfpmod(const std::vector<uint8_t>& s, const std::vector<uint8_t>& res, int32_t expected_error)
{
    bls_fp r{};
    int32_t error = internal_use_do_not_use::bls_fp_mod(
        reinterpret_cast<const char*>(s.data()),
        sizeof(bls_scalar_large),
        reinterpret_cast<char*>(r),
        sizeof(bls_fp)
    );
    check(error == expected_error, "bls_fp_mod: Error does not match");
    check(0 == std::memcmp(r, res.data(), sizeof(bls_fp)), "bls_fp_mod: Result does not match");
}
