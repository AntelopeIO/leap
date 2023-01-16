add_executable(name name.cpp)
target_link_libraries(name abieos)
set_target_properties(name PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ROOT_BINARY_DIR})

add_custom_command(TARGET name POST_BUILD
    COMMAND ln -sf name ${ROOT_BINARY_DIR}/name2num
    COMMAND ln -sf name ${ROOT_BINARY_DIR}/num2name
    COMMAND mkdir -p ${ROOT_BINARY_DIR}/clsdk/bin
    COMMAND cp -a $<TARGET_FILE:name> ${ROOT_BINARY_DIR}/clsdk/bin/name2num
    COMMAND ln -sf name2num ${ROOT_BINARY_DIR}/clsdk/bin/num2name
)
