### Build contracts with cdt if available ###
include(ExternalProject)

set(SPEC_TEST_WASM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/wasms")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/wasm_spec_tests.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/include/wasm_spec_tests.hpp ESCAPE_QUOTES)

### BUILD UNIT TEST EXECUTABLE ###
file(GLOB WASM_TESTS "*.cpp") # find all unit test suites
add_executable( wasm_spec_test ${WASM_TESTS}) # build unit tests as one executable

target_link_libraries( wasm_spec_test eosio_chain_wrap chainbase eosio_testing fc appbase ${PLATFORM_SPECIFIC_LIBS} )

target_compile_options(wasm_spec_test PUBLIC -DDISABLE_EOSLIB_SERIALIZE)
target_include_directories( wasm_spec_test PUBLIC
                            ${CMAKE_SOURCE_DIR}/libraries/testing/include
                            ${CMAKE_CURRENT_BINARY_DIR}/include )

### MARK TEST SUITES FOR EXECUTION ###
foreach(TEST_SUITE ${WASM_TESTS}) # create an independent target for each test suite
  execute_process(COMMAND bash -c "grep -Eo 'BOOST_DATA_TEST_CASE\\(\\w*' '${TEST_SUITE}' | cut -c 22-" OUTPUT_VARIABLE SUITE_NAME OUTPUT_STRIP_TRAILING_WHITESPACE) # get the test suite name from the *.cpp file
  if (NOT "" STREQUAL "${SUITE_NAME}") # ignore empty lines
    string(REPLACE "\n" ";" SN_LIST ${SUITE_NAME})
    foreach(SN ${SN_LIST})
      foreach(RUNTIME ${EOSIO_WASM_RUNTIMES})
          get_test_property(${SN}_unit_test_${RUNTIME} LABELS TEST_LABEL)
          if ("NOTFOUND" STREQUAL "${TEST_LABEL}") # prevent duplicates
            add_test(NAME ${SN}_unit_test_${RUNTIME} COMMAND wasm_spec_test --run_test=${SN} --report_level=detailed --color_output --catch_system_errors=no -- --${RUNTIME})
            set_property(TEST ${SN}_unit_test_${RUNTIME} PROPERTY LABELS wasm_spec_tests)
            # build list of tests to run during coverage testing
            if(ctest_tests)
                string(APPEND ctest_tests "|")
            endif()
            string(APPEND ctest_tests "${SN}_unit_test_${RUNTIME}")
          endif()
      endforeach()
    endforeach(SN)
  endif()
endforeach(TEST_SUITE)
set(ctest_tests "'${ctest_tests}' -j8") # surround test list string in apostrophies
