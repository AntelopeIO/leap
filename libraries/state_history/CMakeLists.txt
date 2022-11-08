add_executable(ship_abi_gen ship_abi_gen.cpp)
target_include_directories( ship_abi_gen  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(ship_abi_gen abieos ${CMAKE_THREAD_LIBS_INIT})
add_custom_command(OUTPUT ship_abi.cpp
                   COMMAND ship_abi_gen > ship_abi.cpp
                   DEPENDS ship_abi_gen)

add_custom_target(ship_abi_app ALL
   COMMAND echo "generate ship_abi.cpp"
   DEPENDS ship_abi.cpp
   VERBATIM
)


file(GLOB HEADERS "include/eosio/state-history/*.hpp")

add_library( state_history
             ${CMAKE_CURRENT_BINARY_DIR}/ship_abi.cpp
             compression.cpp
             create_deltas.cpp
             trace_converter.cpp
             ${HEADERS}
           )

target_link_libraries( state_history
                       PUBLIC eosio_chain fc chainbase softfloat
                     )

target_include_directories( state_history
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/../wasm-jit/Include"
                          )
