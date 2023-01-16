enable_testing()

function(add_abieos suffix)
    add_library(abieos${suffix}
        include/eosio/fpconv.c
        src/abi.cpp
        src/crypto.cpp
    )
    target_link_libraries(abieos${suffix} PUBLIC rapidjson)
    target_include_directories(abieos${suffix} PUBLIC include)

    if(IS_WASM)
        target_link_libraries(abieos${suffix} PUBLIC wasm-base${suffix})
    endif()
endfunction()

add_abieos("")

if(IS_WASM)
    add_abieos("-debug")
    add_abieos("-clsdk")
    add_abieos("-debug-clsdk")

    set_target_properties(abieos-clsdk PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${ROOT_BINARY_DIR}/clsdk/lib-wasm)
    set_target_properties(abieos-clsdk PROPERTIES ARCHIVE_OUTPUT_NAME abieos)
    set_target_properties(abieos-debug-clsdk PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${ROOT_BINARY_DIR}/clsdk/lib-wasm)
    set_target_properties(abieos-debug-clsdk PROPERTIES ARCHIVE_OUTPUT_NAME abieos-debug)

    copy_headers(include/eosio abieos/include/eosio)
    configure_file(include/eosio/fpconv.license ${ROOT_BINARY_DIR}/clsdk/licenses/LICENSE.fpconv COPYONLY)
endif()

if(IS_NATIVE)
    target_sources(abieos PRIVATE src/abieos.cpp)

    add_executable(test-abieos src/test.cpp)
    target_link_libraries(test-abieos abieos)
    set_target_properties(test-abieos PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ROOT_BINARY_DIR})
    native_test(test-abieos)

    add_executable(test-abieos-template src/template_test.cpp)
    target_link_libraries(test-abieos-template abieos)
    set_target_properties(test-abieos-template PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ROOT_BINARY_DIR})
    native_test(test-abieos-template)

    add_executable(test-abieos-key src/key_test.cpp)
    target_link_libraries(test-abieos-key abieos)
    set_target_properties(test-abieos-key PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ROOT_BINARY_DIR})
    native_test(test-abieos-key)

    add_executable(test-abieos-reflect src/reflect_test.cpp)
    target_link_libraries(test-abieos-reflect abieos)
    set_target_properties(test-abieos-reflect PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ROOT_BINARY_DIR})
    native_test(test-abieos-reflect)

    add_subdirectory(tools)
endif()
