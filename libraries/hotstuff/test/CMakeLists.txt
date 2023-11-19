add_executable( test_hotstuff test_hotstuff.cpp test_hotstuff_state.cpp hotstuff_tools.cpp test_pacemaker.cpp)
target_link_libraries( test_hotstuff hotstuff fc Boost::unit_test_framework)

add_test(NAME test_hotstuff COMMAND test_hotstuff WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST test_hotstuff PROPERTY LABELS nonparallelizable_tests)

