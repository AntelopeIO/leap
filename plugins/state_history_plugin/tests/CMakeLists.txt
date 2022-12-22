add_executable( test_state_history_session session_test.cpp )
target_link_libraries(test_state_history_session state_history Boost::unit_test_framework)
target_include_directories( test_state_history_session PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include" )

add_test(test_state_history_session test_state_history_session)