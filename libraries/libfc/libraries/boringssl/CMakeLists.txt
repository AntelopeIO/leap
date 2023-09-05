find_package(Python COMPONENTS Interpreter)
add_custom_target(regenerate_boringssl COMMAND "${Python_EXECUTABLE}" src/util/generate_build_files.py cmake WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/boringssl")

#avoid erroring out the cmake project generation when boringssl's generated files do not exist. The overall build would still fail, of
# course, but the 'regenerate_boringssl' target still will be alive to use
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/boringssl/CMakeLists.txt")
   return()
endif()

add_subdirectory(boringssl EXCLUDE_FROM_ALL)
target_compile_options(fipsmodule PRIVATE -Wno-error)
target_compile_options(crypto PRIVATE -Wno-error)
target_compile_options(decrepit PRIVATE -Wno-error)

#paranoia for when a dependent library depends on openssl (such as libcurl)
set_target_properties(fipsmodule PROPERTIES C_VISIBILITY_PRESET hidden)
set_target_properties(crypto PROPERTIES C_VISIBILITY_PRESET hidden)
set_target_properties(decrepit PROPERTIES C_VISIBILITY_PRESET hidden)

add_library(boringssl INTERFACE)
target_link_libraries(boringssl INTERFACE crypto decrepit)
target_include_directories(boringssl INTERFACE boringssl/src/include)

# avoid conflict with system lib
set_target_properties(crypto PROPERTIES PREFIX libbs)

install( TARGETS crypto 
   LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
)

install( TARGETS decrepit 
   LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
)

install( DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/boringssl/src/include/" DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/leapboringssl" COMPONENT dev EXCLUDE_FROM_ALL )
