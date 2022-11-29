#include <fc/crypto/elliptic.hpp>

#include <fc/crypto/base58.hpp>
#include <fc/crypto/hmac.hpp>
#include <fc/crypto/openssl.hpp>
#include <fc/crypto/sha512.hpp>

#include <fc/fwd_impl.hpp>
#include <fc/exception/exception.hpp>
#include <fc/log/logger.hpp>

#include <secp256k1.h>
#include <secp256k1_recovery.h>

#if _WIN32
# include <malloc.h>
#elif defined(__FreeBSD__)
# include <stdlib.h>
#else
# include <alloca.h>
#endif

#include "_elliptic_impl_priv.hpp"

namespace fc { namespace ecc {
    namespace detail
    {
        const secp256k1_context* _get_context() {
            static secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY | SECP256K1_CONTEXT_SIGN);
            return ctx;
        }

        void _init_lib() {
            static const secp256k1_context* ctx = _get_context();
            (void)ctx;
        }

        class public_key_impl
        {
            public:
                public_key_impl() BOOST_NOEXCEPT
                {
                    _init_lib();
                }

                public_key_impl( const public_key_impl& cpy ) BOOST_NOEXCEPT
                    : _key( cpy._key )
                {
                    _init_lib();
                }

                public_key_data _key;
        };

        typedef fc::array<char,37> chr37;
        chr37 _derive_message( const public_key_data& key, int i );
        fc::sha256 _left( const fc::sha512& v );
        fc::sha256 _right( const fc::sha512& v );
        const ec_group& get_curve();
        const private_key_secret& get_curve_order();
        const private_key_secret& get_half_curve_order();
    }

    static const public_key_data empty_pub;
    

    fc::sha512 private_key::get_shared_secret( const public_key& other )const
    {
      static const private_key_secret empty_priv;
      FC_ASSERT( my->_key != empty_priv );
      FC_ASSERT( other.my->_key != empty_pub );
      secp256k1_pubkey secp_pubkey;
      FC_ASSERT( secp256k1_ec_pubkey_parse( detail::_get_context(), &secp_pubkey, (unsigned char*)other.serialize().data, other.serialize().size() ) );
      FC_ASSERT( secp256k1_ec_pubkey_tweak_mul( detail::_get_context(), &secp_pubkey, (unsigned char*) my->_key.data() ) );
      public_key_data serialized_result;
      size_t serialized_result_sz = sizeof(serialized_result);
      secp256k1_ec_pubkey_serialize(detail::_get_context(), (unsigned char*)&serialized_result.data, &serialized_result_sz, &secp_pubkey, SECP256K1_EC_COMPRESSED );
      FC_ASSERT( serialized_result_sz == sizeof(serialized_result) );
      return fc::sha512::hash( serialized_result.begin() + 1, serialized_result.size() - 1 );
    }


    public_key::public_key() {}

    public_key::public_key( const public_key &pk ) : my( pk.my ) {}

    public_key::public_key( public_key &&pk ) : my( std::move( pk.my ) ) {}

    public_key::~public_key() {}

    public_key& public_key::operator=( const public_key& pk )
    {
        my = pk.my;
        return *this;
    }

    public_key& public_key::operator=( public_key&& pk )
    {
        my = pk.my;
        return *this;
    }

    bool public_key::valid()const
    {
      return my->_key != empty_pub;
    }

    std::string public_key::to_base58() const
    {
        FC_ASSERT( my->_key != empty_pub );
        return to_base58( my->_key );
    }

    public_key_data public_key::serialize()const
    {
        FC_ASSERT( my->_key != empty_pub );
        return my->_key;
    }

    public_key::public_key( const public_key_point_data& dat )
    {
        const char* front = &dat.data[0];
        if( *front == 0 ){}
        else
        {
            EC_KEY *key = EC_KEY_new_by_curve_name( NID_secp256k1 );
            key = o2i_ECPublicKey( &key, (const unsigned char**)&front, sizeof(dat) );
            FC_ASSERT( key );
            EC_KEY_set_conv_form( key, POINT_CONVERSION_COMPRESSED );
            unsigned char* buffer = (unsigned char*) my->_key.begin();
            i2o_ECPublicKey( key, &buffer ); // FIXME: questionable memory handling
            EC_KEY_free( key );
        }
    }

    public_key::public_key( const public_key_data& dat )
    {
        my->_key = dat;
    }

    public_key::public_key( const compact_signature& c, const fc::sha256& digest, bool check_canonical )
    {
        int nV = c.data[0];
        if (nV<27 || nV>=35)
            FC_THROW_EXCEPTION( exception, "unable to reconstruct public key from signature" );

        if( check_canonical )
        {
            FC_ASSERT( is_canonical( c ), "signature is not canonical" );
        }

        secp256k1_pubkey secp_pub;
        secp256k1_ecdsa_recoverable_signature secp_sig;

        FC_ASSERT( secp256k1_ecdsa_recoverable_signature_parse_compact( detail::_get_context(), &secp_sig, (unsigned char*)c.begin() + 1, (*c.begin() - 27) & 3) );
        FC_ASSERT( secp256k1_ecdsa_recover( detail::_get_context(), &secp_pub, &secp_sig, (unsigned char*) digest.data() ) );

        size_t serialized_result_sz = my->_key.size();
        secp256k1_ec_pubkey_serialize( detail::_get_context(), (unsigned char*)&my->_key.data, &serialized_result_sz, &secp_pub, SECP256K1_EC_COMPRESSED );
        FC_ASSERT( serialized_result_sz == my->_key.size() );
    }

} }
