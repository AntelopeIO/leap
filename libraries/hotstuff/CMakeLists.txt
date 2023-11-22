file(GLOB HEADERS "include/eosio/hotstuff/*.hpp")

add_library( hotstuff
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

install(TARGETS hotstuff
        LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
        ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL)
install(DIRECTORY include/eosio/hotstuff/
        DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR}/eosio/hotstuff
        COMPONENT dev EXCLUDE_FROM_ALL)
