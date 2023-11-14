file(GLOB BENCHMARK "*.cpp")
add_executable( benchmark ${BENCHMARK} )

target_link_libraries( benchmark eosio_testing fc Boost::program_options bn256)
target_include_directories( benchmark PUBLIC
                            "${CMAKE_CURRENT_SOURCE_DIR}"
                            "${CMAKE_CURRENT_BINARY_DIR}/../unittests/include"
                          )
