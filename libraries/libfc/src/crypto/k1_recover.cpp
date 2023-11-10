#include <fc/exception/exception.hpp>
#include <fc/crypto/k1_recover.hpp>
#include <secp256k1.h>
#include <secp256k1_recovery.h>

namespace fc {

    const secp256k1_context* k1_recover_context() {
        static secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
        return ctx;
    }

    std::variant<k1_recover_error, bytes> k1_recover(const bytes& signature, const bytes& digest) {
        const secp256k1_context* context{k1_recover_context()};
        FC_ASSERT(context != nullptr);

        if (signature.size() != 65 || digest.size() != 32) {
            return k1_recover_error::input_error;
        }

        int recid = signature[0];
        if (recid<27 || recid>=35) return k1_recover_error::invalid_signature;
        recid = (recid - 27) & 3;

        secp256k1_ecdsa_recoverable_signature sig;
        if (!secp256k1_ecdsa_recoverable_signature_parse_compact(context, &sig, (const unsigned char*)&signature[1], recid)) {
            return k1_recover_error::invalid_signature;
        }

        secp256k1_pubkey pub_key;
        if (!secp256k1_ecdsa_recover(context, &pub_key, &sig, (const unsigned char*)&digest[0])) {
            return k1_recover_error::recover_error;
        }

        size_t kOutLen{65};
        bytes out(kOutLen, '\0');
        secp256k1_ec_pubkey_serialize(context, (unsigned char*)&out[0], &kOutLen, &pub_key, SECP256K1_EC_UNCOMPRESSED);
        return out;
    }
}
