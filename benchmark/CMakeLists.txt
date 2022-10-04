file(GLOB BENCHMARK "*.cpp")
add_executable( benchmark ${BENCHMARK} bn_256.cpp)

target_link_libraries( benchmark fc bn256 Boost::program_options )
target_include_directories( benchmark PUBLIC
                            "${CMAKE_CURRENT_SOURCE_DIR}"
                          )
