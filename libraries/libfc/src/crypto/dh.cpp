#include <fc/crypto/dh.hpp>
#include <openssl/dh.h>

namespace fc {
    SSL_TYPE(ssl_dh, DH, DH_free)

    static bool validate( const ssl_dh& dh, bool& valid ) {
        int check;
        DH_check(dh,&check);
        return valid = !(check /*& DH_CHECK_P_NOT_SAFE_PRIME*/);
    }

   bool diffie_hellman::generate_params( int s, uint8_t g )
   {
        ssl_dh dh;
        DH_generate_parameters_ex(dh.obj, s, g, NULL);
        ssl_bignum bn_p;
        DH_get0_pqg(dh.obj, (const BIGNUM**)&bn_p.obj, NULL, NULL);
        p.resize( BN_num_bytes( bn_p ) );
        if( p.size() )
            BN_bn2bin( bn_p, (unsigned char*)&p.front()  );
        this->g = g;
        return fc::validate( dh, valid );
   }

   bool diffie_hellman::validate()
   {
        if( !p.size() ) 
            return valid = false;
        ssl_dh dh = DH_new();
        const auto bn_p = BN_bin2bn( (unsigned char*)&p.front(), p.size(), NULL );
        const auto bn_g = BN_bin2bn( (unsigned char*)&g, 1, NULL );
        DH_set0_pqg(dh.obj, bn_p, NULL, bn_g);
        return fc::validate( dh, valid );
   }

   bool diffie_hellman::generate_pub_key()
   {
        if( !p.size() ) 
            return valid = false;
        ssl_dh dh = DH_new();
        const auto bn_p = BN_bin2bn( (unsigned char*)&p.front(), p.size(), NULL );
        const auto bn_g = BN_bin2bn( (unsigned char*)&g, 1, NULL );
        DH_set0_pqg(dh.obj, bn_p, NULL, bn_g);

        if( !fc::validate( dh, valid ) )
        {
            return false;
        }
        DH_generate_key(dh);

        ssl_bignum bn_pub_key;
        ssl_bignum bn_priv_key;
        DH_get0_key(dh.obj, (const BIGNUM**)&bn_pub_key.obj, (const BIGNUM**)&bn_priv_key.obj);
        pub_key.resize( BN_num_bytes( bn_pub_key ) );
        priv_key.resize( BN_num_bytes( bn_priv_key ) );
        if( pub_key.size() )
            BN_bn2bin( bn_pub_key.obj, (unsigned char*)&pub_key.front()  );
        if( priv_key.size() )
            BN_bn2bin( bn_priv_key.obj, (unsigned char*)&priv_key.front()  );

        return true;
   }
   bool diffie_hellman::compute_shared_key( const char* buf, uint32_t s ) {
        ssl_dh dh = DH_new();
        auto bn_p = BN_bin2bn( (unsigned char*)&p.front(), p.size(), NULL );
        auto bn_pub_key = BN_bin2bn( (unsigned char*)&pub_key.front(), pub_key.size(), NULL );
        auto bn_priv_key = BN_bin2bn( (unsigned char*)&priv_key.front(), priv_key.size(), NULL );
        auto bn_g = BN_bin2bn( (unsigned char*)&g, 1, NULL );
        DH_set0_pqg(dh.obj, bn_p, NULL, bn_g);
        DH_set0_key(dh.obj, bn_pub_key, bn_priv_key);

        int check;
        DH_check(dh,&check);
        if( !fc::validate( dh, valid ) )
        {
            return false;
        }

        ssl_bignum pk;
        BN_bin2bn( (unsigned char*)buf, s, pk );
        shared_key.resize( DH_size(dh) ); 
        DH_compute_key( (unsigned char*)&shared_key.front(), pk, dh );

        return true;
   }
   bool diffie_hellman::compute_shared_key( const std::vector<char>& pubk ) {
      return compute_shared_key( &pubk.front(), pubk.size() );
   }
}
