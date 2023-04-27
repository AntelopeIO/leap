file(GLOB UNIT_TESTS
        "*.cpp"
        "crypto/*.cpp"
        "io/*.cpp"
        "network/*.cpp"
        "scoped_exit/*.cpp"
        "static_variant/*.cpp"
        "variant/*.cpp"
        "variant_estimated_size/*.cpp")
add_executable( test_fc ${UNIT_TESTS} )
target_link_libraries( test_fc fc )

add_test(NAME test_fc COMMAND test_fc WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
