add_executable( test_static_variant test_static_variant.cpp)
target_link_libraries( test_static_variant fc )

add_test(NAME test_static_variant COMMAND test_static_variant WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
