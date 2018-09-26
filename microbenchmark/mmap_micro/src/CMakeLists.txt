###
# project setting
project (mmap_mb)
cmake_minimum_required (VERSION 2.8)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

###
# compiler options
# - set compiler-dependent options
set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")

# - set general options

#set(COMMON_FLAGS  "-march=native -mtune=native -fno-rtti ${TARGET_OPT} ${COMP_DEP_OPT}")
set(DEBUG_FLAGS   "-O0 -g -fno-omit-frame-pointer")
set(RELEASE_FLAGS "-O3")

set(CMAKE_CXX_FLAGS "-std=gnu++0x ${COMMON_FLAGS} ${DEBUG_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "-std=gnu++0x ${COMMON_FLAGS} ${RELEASE_FLAGS}")

# - where to find cmake modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake_modules")

set(MMAP_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

include_directories(${MMAP_INCLUDE_DIR})

add_subdirectory(lib)
