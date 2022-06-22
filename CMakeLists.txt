cmake_minimum_required(VERSION 3.8.0)

project(NP_schedulabiliy_test VERSION 1.1.0 LANGUAGES CXX)

include_directories(include)
include_directories(lib/include)

option(USE_TBB_MALLOC "Use the Intel TBB scalable memory allocator" OFF)
option(USE_JE_MALLOC "Use the Facebook jemalloc scalable memory allocator" OFF)
option(COLLECT_SCHEDULE_GRAPHS "Enable the collection of schedule graphs" ON)
option(DEBUG "Enable debugging" OFF)

if (USE_TBB_MALLOC AND COLLECT_SCHEDULE_GRAPHS)
    message(FATAL_ERROR "Cannot use the Intel TBB scalable memory allocator and collect schedule graphs options at the same time")
elseif (USE_JE_MALLOC AND COLLECT_SCHEDULE_GRAPHS)
    message(FATAL_ERROR "Cannot use the Facebook jemalloc scalable memory allocator and collect schedule graphs options at the same time")
endif ()

if (DEBUG)
    set(CMAKE_BUILD_TYPE Debug)
    message(NOTICE "Debug build")
else ()
    set(CMAKE_BUILD_TYPE Release)
endif ()

if (COLLECT_SCHEDULE_GRAPHS)
    add_definitions(-DCONFIG_COLLECT_SCHEDULE_GRAPH)
elseif (USE_JE_MALLOC)
    find_library(ALLOC_LIB NAMES jemalloc)
elseif (USE_TBB_MALLOC)
    find_package(TBB REQUIRED)
    find_library(TBB_ALLOC tbbmalloc_proxy)
    set(TBB_LIB TBB::tbb)
endif ()

set(CORE_LIBS ${TBB_LIB} ${ALLOC_LIB})

file(GLOB TEST_SOURCES "src/tests/*.cpp")
add_executable(runtests ${TEST_SOURCES} ${SOURCES})

set(NPTEST_SOURCES src/nptest.cpp lib/src/OptionParser.cpp)
add_executable(nptest ${NPTEST_SOURCES})

target_link_libraries(nptest ${CORE_LIBS})
target_link_libraries(runtests ${CORE_LIBS})

target_compile_features(runtests PUBLIC cxx_std_14)
target_compile_features(nptest PUBLIC cxx_std_14)


