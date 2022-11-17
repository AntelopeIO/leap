if (NOT CMAKE_CROSSCOMPILING)
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
else()
  configure_file(ship_abi.cpp ship_abi.cpp COPYONLY)
endif()


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

if (NOT CMAKE_CROSSCOMPILING)
  add_custom_target(BUILD_SUCCESSFUL ALL
                    DEPENDS state_history
                    COMMAND ${CMAKE_COMMAND} -E compare_files ${CMAKE_CURRENT_SOURCE_DIR}/ship_abi.cpp ${CMAKE_CURRENT_BINARY_DIR}/ship_abi.cpp ||
                            ${CMAKE_COMMAND} -E cmake_echo_color --switch=$\{COLOR\} --cyan
                            "======================================================================================================================================================="
                            "Warning: The newly generated ${CMAKE_CURRENT_BINARY_DIR}/ship_abi.cpp is different from "
                            "${CMAKE_CURRENT_SOURCE_DIR}/ship_abi.cpp. If it is becaue you have changed ship_protocol.hpp, please copy "
                            "${CMAKE_CURRENT_BINARY_DIR}/ship_abi.cpp to ${CMAKE_CURRENT_SOURCE_DIR}/ship_abi.cpp and check in the file."
                            "======================================================================================================================================================="
                    VERBATIM)
endif()


add_library(ship_protocol INTERFACE)
target_link_libraries(ship_protocol INTERFACE abieos)
target_include_directories(ship_protocol INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/eosio/ship_protocol.hpp
        DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR}/eosio/
        COMPONENT dev EXCLUDE_FROM_ALL)