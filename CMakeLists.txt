cmake_minimum_required(VERSION 3.8.0)

project(NP_schedulabiliy_test VERSION 3.0.0 LANGUAGES CXX)

include_directories(include)
include_directories(lib/include)

option(PARALLEL_RUN "Enable parallel run" OFF)
option(USE_TBB_MALLOC "Use the Intel TBB scalable memory allocator" OFF)
option(USE_JE_MALLOC "Use the Facebook jemalloc scalable memory allocator" OFF)
option(COLLECT_SCHEDULE_GRAPHS "Enable the collection of schedule graphs (disables parallel)" OFF)
option(DEBUG "Enable debugging" OFF)

if (PARALLEL_RUN AND COLLECT_SCHEDULE_GRAPHS)
    message(FATAL_ERROR "Parallel run and schedule graph collection cannot be enabled at the same time")
endif ()

if (DEBUG)
    set(CMAKE_BUILD_TYPE Debug)
    message(NOTICE "Debug build")
else ()
    set(CMAKE_BUILD_TYPE Release)
endif ()

if (PARALLEL_RUN OR USE_TBB_MALLOC)
    find_package(TBB REQUIRED)
endif()

if (COLLECT_SCHEDULE_GRAPHS)
    add_compile_definitions(CONFIG_COLLECT_SCHEDULE_GRAPH)
    if (PARALLEL_RUN)
        message(WARNING "Schedule graph collection is not supported in parallel mode")
    endif()
elseif (PARALLEL_RUN)
    set(TBB_LIB TBB::tbb)
    add_compile_definitions(CONFIG_PARALLEL)
endif ()

if (USE_JE_MALLOC)
    find_library(ALLOC_LIB NAMES jemalloc)
    message(NOTICE "Using Facebook jemalloc scalable memory allocator")
elseif (USE_TBB_MALLOC)
    set(ALLOC_LIB TBB::tbbmalloc_proxy)
    message(NOTICE "Using Intel TBB scalable memory allocator")
endif ()

###########################################################################
# this is a fix for possible problems with cmake in windows 11 and visual studio 2022
# Option to enable/disable finding yaml-cpp
option(USE_YAML_CPP_PACKAGE "Use yaml-cpp" ON)

# Check if yaml-cpp is installed as a system library and
# cache the result of find_package
if (USE_YAML_CPP_PACKAGE)
    find_package(yaml-cpp)
    if (NOT yaml-cpp_FOUND)
        set(USE_YAML_CPP_PACKAGE OFF CACHE BOOL "Use yaml-cpp" FORCE)
    else ()
        message(STATUS "Using yaml-cpp config file: ${yaml-cpp_CONFIG}")
        message(STATUS "yaml-cpp include: ${YAML_CPP_INCLUDE_DIR}")
        message(STATUS "yaml-cpp library: ${YAML_CPP_LIBRARY_DIR}")
        include_directories(${YAML_CPP_INCLUDE_DIR})
        link_directories(${YAML_CPP_LIBRARY_DIR})
    endif ()
endif ()

# Cache the result of find_package using CMAKE_CACHE_ARGS
set(YAML_CPP_PACKAGE_PATH "" CACHE PATH "Path to yaml-cpp")
###########################################################################

# Check if yaml-cpp is installed as a system library
if (yaml-cpp_FOUND OR YAML_CPP_PACKAGE_PATH)
    message(NOTICE "yaml-cpp found")
else ()
    # Include yaml-cpp source directory and add it to the build
    message(NOTICE "yaml-cpp not found: adding it to the build as a subdirectory")
    add_subdirectory (${CMAKE_CURRENT_LIST_DIR}/lib/yaml-cpp)
endif ()

set(YAML_LIB yaml-cpp)
set(CORE_LIBS ${TBB_LIB} ${ALLOC_LIB} ${YAML_LIB})

file(GLOB TEST_SOURCES "src/tests/*.cpp")
add_executable(runtests ${TEST_SOURCES} ${SOURCES})

set(NPTEST_SOURCES src/nptest.cpp lib/src/OptionParser.cpp)
add_executable(nptest ${NPTEST_SOURCES})

target_link_libraries(nptest ${CORE_LIBS})
target_link_libraries(runtests ${CORE_LIBS})

target_compile_features(runtests PUBLIC cxx_std_14)
target_compile_features(nptest PUBLIC cxx_std_14)

if (MSVC)
    target_compile_options(runtests PUBLIC "/Zc:__cplusplus")
    target_compile_options(nptest PUBLIC "/Zc:__cplusplus")
endif()