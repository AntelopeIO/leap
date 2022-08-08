configure_file(${CMAKE_CURRENT_SOURCE_DIR}/performance_test_basic.py ${CMAKE_CURRENT_BINARY_DIR}/performance_test_basic.py COPYONLY)

add_test(NAME performance_test_basic COMMAND tests/performance_tests/performance_test_basic.py -v -p 1 -n 1 --clean-run --dump-error-detail WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST performance_test_basic PROPERTY LABELS nonparallelizable_tests)
