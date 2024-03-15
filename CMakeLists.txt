cmake_minimum_required(VERSION 3.12)

project(wows-depack)

# Project version (sematic versionning)
set(wows-depack_VERSION_MAJOR 0)
set(wows-depack_VERSION_MINOR 2)
set(wows-depack_VERSION_PATCH 0)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")

if(STATIC)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
  set(ZLIB_USE_STATIC_LIBS ON)
  set(BUILD_SHARED_LIBRARIES OFF)
  set(CMAKE_EXE_LINKER_FLAGS "-static")
else(STATIC)
  set(SHARED "SHARED")
endif(STATIC)

find_package(ZLIB REQUIRED)
find_package(PCRE REQUIRED)
find_library(CUNIT_LIBRARY cunit)

set(wows-depack_VERSION
    ${wows-depack_VERSION_MAJOR}.${wows-depack_VERSION_MINOR}.${wows-depack_VERSION_PATCH}
)

add_custom_target(
  tag
  COMMAND git tag -a ${wows-depack_VERSION} -m
          "tagging version ${wows-depack_VERSION}"
  COMMAND git push origin ${wows-depack_VERSION})

# set version as a definition
set(CMAKE_C_FLAGS
    "${CMAKE_CXX_FLAGS} -DBFD_VERSION='\"${wows-depack_VERSION}\"'")
set(CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} -DBFD_VERSION='\"${wows-depack_VERSION}\"'")

# Options
option(DEBUG "compile with debug symbol" OFF)
option(STATIC "compile statically" OFF)
option(COVERAGE "Enable code coverage" OFF)
option(BUILD_DOC "Build documentation" OFF)
option(BUILD_TESTS "Build tests" OFF)

if(DEBUG)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g")
  set(CMAKE_BUILD_TYPE Debug)
endif(DEBUG)

# Build external dependancies if we are on OSX
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  # Mac OS X specific code
  set(EXTERNAL_ICONV "iconv")
  set(EXTERNAL_ARGP "argp")
  add_definitions(-DDARWIN)
endif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")

if(UNIX)
  link_libraries(m)
endif(UNIX)

# linking directories
link_directories(${CMAKE_BINARY_DIR}/ /usr/local/lib /usr/lib/)

# headers directories
include_directories(./inc/ ${ZLIB_INCLUDE_DIRS} ${PCRE_INCLUDE_DIRS}
                    /usr/include/ /sw/include/)

file(GLOB WOWS_SOURCES lib/*.c)
add_library(wows-depack ${SHARED} ${WOWS_SOURCES})
set_target_properties(
  wows-depack PROPERTIES VERSION ${wows-depack_VERSION}
                         SOVERSION ${wows-depack_VERSION_MAJOR})

# Compile the executable
add_executable(wows-depack-cli wows-depack.c)
target_link_libraries(wows-depack ${ZLIB_LIBRARIES} ${PCRE_LIBRARY})

target_link_libraries(wows-depack-cli wows-depack ${ZLIB_LIBRARIES}
                      ${PCRE_LIBRARY})

if(BUILD_TESTS)
  if(CUNIT_LIBRARY)

    include(CTest)
    add_test(NAME wows-depack-test COMMAND wows-depack-test)
    add_test(NAME hashmap-test COMMAND hashmap-test)
    add_test(NAME hashmap-bench COMMAND hashmap-test)
    set_property(TEST hashmap-bench PROPERTY ENVIRONMENT "BENCH=1")
    add_executable(wows-depack-test tests/tests.c)
    add_executable(hashmap-test lib/hashmap.c)
    set_target_properties(hashmap-test PROPERTIES COMPILE_FLAGS
                                                  "-DHASHMAP_TEST")
    target_link_libraries(wows-depack-test wows-depack ${ZLIB_LIBRARIES}
                          ${PCRE_LIBRARY} ${CUNIT_LIBRARY})

    # Add the test target
    add_custom_target(
      tests
      COMMAND wows-depack-test
      COMMAND hashmap-test
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Running tests")
    add_dependencies(tests wows-depack-test)

    if(COVERAGE)
      set(CMAKE_BUILD_TYPE Debug)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage")
      include(CodeCoverage)
      setup_target_for_coverage(
        NAME
        coverage
        EXECUTABLE
        ctest
        -j
        ${n_cores}
        DEPENDENCIES
        wows-depack-test
        hashmap-test)

      add_dependencies(coverage wows-depack-test)
    endif(COVERAGE)

  else(CUNIT_LIBRARY)
    message(WARNING "CUnit not found; not building tests.")
  endif(CUNIT_LIBRARY)
endif(BUILD_TESTS)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -Wall")

if(NOT LIB_INSTALL_DIR)
  set(LIB_INSTALL_DIR lib)
endif()

if(NOT BIN_INSTALL_DIR)
  set(BIN_INSTALL_DIR bin)
endif()

if(NOT INCLUDE_INSTALL_DIR)
  set(INCLUDE_INSTALL_DIR include)
endif()

# install binaries and library
install(
  TARGETS wows-depack wows-depack-cli
  RUNTIME DESTINATION ${BIN_INSTALL_DIR}
  LIBRARY DESTINATION ${LIB_INSTALL_DIR}
  ARCHIVE DESTINATION ${LIB_INSTALL_DIR})

# check if Doxygen is installed
if(BUILD_DOC)
  # set input and output files
  find_package(Doxygen)
  if(DOXYGEN_FOUND)
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    # request to configure the file
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    message("Doxygen build started")

    # note the option ALL which allows to build the docs together with the
    # application
    add_custom_target(
      doc_doxygen ALL
      COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      COMMENT "Generating API documentation with Doxygen"
      VERBATIM)
  else(DOXYGEN_FOUND)
    message(
      "Doxygen need to be installed to generate the doxygen documentation")
  endif(DOXYGEN_FOUND)
endif(BUILD_DOC)

# install header file
install(FILES inc/wows-depack.h DESTINATION ${INCLUDE_INSTALL_DIR})
