add_library(leap-cli11 INTERFACE)

set(CLI11_TESTING OFF)
add_subdirectory(cli11)
mark_as_advanced(CLI_CXX_STD CLI_EXAMPLES CLI_SINGLE_FILE CLI_SINGLE_FILE_TESTS CLI_TESTING)
target_link_libraries(leap-cli11 INTERFACE CLI11::CLI11)

