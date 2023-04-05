file(GLOB HEADERS "include/eosio/hotstuff/*.hpp")

add_library( hotstuff
             test_pacemaker.cpp
             chain_pacemaker.cpp
             qc_chain.cpp
             ${HEADERS}
           )

target_link_libraries( hotstuff 
                       PUBLIC eosio_chain fc
                     )

target_include_directories( hotstuff
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" 
                          )

add_subdirectory( test )
