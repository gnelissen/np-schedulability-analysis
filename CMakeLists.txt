cmake_minimum_required(VERSION 3.8.0)

project(NP_schedulabiliy_test VERSION 2.1.0 LANGUAGES CXX)

include_directories(include)
include_directories(lib/include)

option(PARALLEL_RUN "Enable parallel run" OFF)
option(USE_TBB_MALLOC "Use the Intel TBB scalable memory allocator" ON)
option(USE_JE_MALLOC "Use the Facebook jemalloc scalable memory allocator" OFF)
option(COLLECT_SCHEDULE_GRAPHS "Enable the collection of schedule graphs" OFF)
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

if (COLLECT_SCHEDULE_GRAPHS)
    add_compile_definitions(CONFIG_COLLECT_SCHEDULE_GRAPH)
elseif (PARALLEL_RUN)
    find_package(TBB REQUIRED)
    set(TBB_LIB TBB::tbb)
    find_path(TBB_INCLUDE_DIR NAMES tbb/task_scheduler_init.h)
    include_directories(${TBB_INCLUDE_DIR})
    add_compile_definitions(CONFIG_PARALLEL)
endif ()

if (USE_JE_MALLOC)
    find_library(ALLOC_LIB NAMES jemalloc)
    message(NOTICE "Using Facebook jemalloc scalable memory allocator")
elseif (USE_TBB_MALLOC)
    find_library(ALLOC_LIB tbbmalloc_proxy)
    message(NOTICE "Using Intel TBB scalable memory allocator")
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


