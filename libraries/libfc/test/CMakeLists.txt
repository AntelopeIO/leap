add_executable( test_fc
        crypto/test_blake2.cpp
        crypto/test_bls.cpp
        crypto/test_cypher_suites.cpp
        crypto/test_hash_functions.cpp
        crypto/test_k1_recover.cpp
        crypto/test_modular_arithmetic.cpp
        crypto/test_webauthn.cpp
        io/test_cfile.cpp
        io/test_json.cpp
        io/test_tracked_storage.cpp
        network/test_message_buffer.cpp
        scoped_exit/test_scoped_exit.cpp
        static_variant/test_static_variant.cpp
        variant/test_variant.cpp
        variant_estimated_size/test_variant_estimated_size.cpp
        test_base64.cpp
        test_escape_str.cpp
        main.cpp
        )
target_link_libraries( test_fc fc )

add_test(NAME test_fc COMMAND test_fc WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
