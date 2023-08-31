configure_file(__init__.py . COPYONLY)
configure_file(BasePluginArgs.py . COPYONLY)

set(GEN_FILES
        ChainPluginArgs.py
        HttpPluginArgs.py
        NetPluginArgs.py
        ProducerPluginArgs.py
        ResourceMonitorPluginArgs.py
        SignatureProviderPluginArgs.py
        StateHistoryPluginArgs.py
        TraceApiPluginArgs.py)

add_custom_command(COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate_nodeos_plugin_args_class_files.py
                   WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                   DEPENDS ${NODE_EXECUTABLE_NAME}
                   OUTPUT ${GEN_FILES})

add_custom_target(GenNodeosPluginArgs ALL
                  DEPENDS ${GEN_FILES}
                  COMMENT "Generating NodeosPluginArgs files")
