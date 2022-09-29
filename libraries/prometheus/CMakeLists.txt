# Defines ChainBase library target.
project( Prometheus )
cmake_minimum_required( VERSION 3.5 )


#set_target_properties(core
#        PROPERTIES
#        OUTPUT_NAME ${PROJECT_NAME}-core
#        DEFINE_SYMBOL PROMETHEUS_CPP_CORE_EXPORTS
#        VERSION "${PROJECT_VERSION}"
#        SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
#        )

#generate_export_header(core
#        BASE_NAME ${PROJECT_NAME}-core
#        EXPORT_FILE_NAME include/prometheus/detail/core_export.h
#        )


if( "${CMAKE_GENERATOR}" STREQUAL "Ninja" )
  if( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" )
      set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcolor-diagnostics" )
  endif()
endif()

set(ENABLE_COVERAGE_TESTING FALSE CACHE BOOL "Build Prometheus for code coverage analysis")

if(ENABLE_COVERAGE_TESTING)
    SET(CMAKE_CXX_FLAGS "--coverage ${CMAKE_CXX_FLAGS}")
endif()

file(GLOB HEADERS "include/prometheus/*.hpp")
add_library( prometheus ${HEADERS} src/counter.cpp)
target_include_directories( prometheus PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"  )
#set_target_properties(prometheus PROPERTIES LINKER_LANGUAGE CXX)

if(PROMETHEUS_INSTALL_COMPONENT)
   set(INSTALL_COMPONENT_ARGS COMPONENT ${PROMETHEUS_INSTALL_COMPONENT} EXCLUDE_FROM_ALL)
endif()

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/prometheus DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR} ${INSTALL_COMPONENT_ARGS})

install(TARGETS prometheus
   LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} ${INSTALL_COMPONENT_ARGS}
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} ${INSTALL_COMPONENT_ARGS})

