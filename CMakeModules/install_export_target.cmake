function(install_export_targets)
  set(options EXCLUDE_FROM_ALL)
  set(oneValueArgs EXPORT COMPONENT)
  set(multiValueArgs TARGETS DESTINATIONS)
  cmake_parse_arguments(MY_INSTALL "${options}" "${oneValueArgs}"
                        "${multiValueArgs}" ${ARGN})

  if(MY_INSTALL_EXCLUDE_FROM_ALL)
    set(exclude_from_all EXCLUDE_FROM_ALL)
  endif()

  if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.13.0")
    install(
      TARGETS ${MY_INSTALL_TARGETS}
      EXPORT "${MY_INSTALL_EXPORT}"
      COMPONENT "${MY_INSTALL_COMPONENT}"
      ${exclude_from_all})

    foreach(_destination ${MY_INSTALL_DESTINATIONS})
      install(
        EXPORT "${MY_INSTALL_EXPORT}"
        DESTINATION ${_destination}
        FILE "${MY_INSTALL_EXPORT}-targets.cmake"
        COMPONENT "${MY_INSTALL_COMPONENT}"
        ${exclude_from_all})
    endforeach()

  else()

    set(target_config_content
        "#----------------------------------------------------------------
# Generated CMake target import file for configuration \"$<CONFIG>\".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)\n")

    foreach(_lib ${MY_INSTALL_TARGETS})
      set(_properties)
      get_target_property(_compile_definitions ${_lib}
                          INTERFACE_COMPILE_DEFINITIONS)
      if(_compile_definitions)
        set(_properties
            "${_properties}  INTERFACE_COMPILE_DEFINITIONS \"${_compile_definitions}\"\n"
        )
      endif()

      get_target_property(_compile_options ${_lib}
                          INTERFACE_COMPILE_OPTIONS)
      if(_compile_options)
        string(REGEX REPLACE "\\$" "\\\\$" _compile_options "${_compile_options}")
        set(_properties
            "${_properties}  INTERFACE_COMPILE_OPTIONS \"${_compile_options}\"\n"
        )
      endif()

      get_target_property(_install_include_dir ${_lib}
                          INTERFACE_INCLUDE_DIRECTORIES)
      string(REGEX REPLACE "\\$<BUILD_INTERFACE:[^>]*>;?" ""
                           _install_include_dir "${_install_include_dir}")
      string(
        REGEX
        REPLACE "\\$<INSTALL_INTERFACE:([^>]*)>;?" "\${_IMPORT_PREFIX}/\\1"
                _install_include_dir "${_install_include_dir}")
      if(_install_include_dir)
        set(_properties
            "${_properties}  INTERFACE_INCLUDE_DIRECTORIES \"${_install_include_dir}\"\n"
        )
      endif()

      get_target_property(_link_libraries ${_lib} INTERFACE_LINK_LIBRARIES)
      if(_link_libraries)
        string(REGEX REPLACE "\\$" "\\\\$" _link_libraries "${_link_libraries}")
        set(_properties
            "${_properties}  INTERFACE_LINK_LIBRARIES \"${_link_libraries}\"\n")
      endif()

      if(_properties)
        set(_properties
            "\nset_target_properties(${_lib} PROPERTIES\n${_properties})\n")
      endif()

      get_target_property(target_type ${_lib} TYPE)

      if("${target_type}" STREQUAL "INTERFACE_LIBRARY")
        set(add_lib_entry
            "${add_lib_entry}
# Create imported target ${_lib}
add_library(${_lib} INTERFACE IMPORTED)
${_properties}")

      else()

        set(add_lib_entry
            "${add_lib_entry}
# Create imported target ${_lib}
add_library(${_lib} STATIC IMPORTED)
${_properties}")

        set(target_config_content
            "${target_config_content}
# Import target \"${_lib}\" for configuration \"$<CONFIG>\"
set_property(TARGET ${_lib} APPEND PROPERTY IMPORTED_CONFIGURATIONS $<UPPER_CASE:$<CONFIG>>)
set_target_properties(${_lib} PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_$<UPPER_CASE:$<CONFIG>> \"$<TARGET_PROPERTY:${_lib},LINKER_LANGUAGE>\"
  IMPORTED_LOCATION_$<UPPER_CASE:$<CONFIG>> \"\${_IMPORT_PREFIX}/lib/$<TARGET_FILE_NAME:${_lib}>\"
  )

list(APPEND _cmake_import_check_targets ${_lib} )
list(APPEND _cmake_import_check_files_for_${_lib} \"\${_IMPORT_PREFIX}/lib/$<TARGET_FILE_NAME:${_lib}>\" )
")
      endif()

    endforeach()
    configure_file(
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules/install_targets.cmake.in
      ${MY_INSTALL_EXPORT}-targets.cmake @ONLY)

    set(target_config_content
        "${target_config_content}
# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
")

    file(
      GENERATE
      OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/${MY_INSTALL_EXPORT}-targets-$<LOWER_CASE:$<CONFIG>>.cmake
      CONTENT "${target_config_content}")

    foreach(_destination ${MY_INSTALL_DESTINATIONS})
      install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/${MY_INSTALL_EXPORT}-targets.cmake"
        DESTINATION "${_destination}"
        COMPONENT "${MY_INSTALL_COMPONENT}"
        ${exclude_from_all})

      install(
        FILES
          "${CMAKE_CURRENT_BINARY_DIR}/${MY_INSTALL_EXPORT}-targets-$<LOWER_CASE:$<CONFIG>>.cmake"
        DESTINATION "${_destination}"
        COMPONENT "${MY_INSTALL_COMPONENT}"
        ${exclude_from_all})
    endforeach()
  endif()
endfunction()
