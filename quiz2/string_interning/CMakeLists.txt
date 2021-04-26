cmake_minimum_required (VERSION 3.0.0)

project (Cstr)

set (CSTR_LIB_NAME cstr)
set (CSTR_LIB_TYPE SHARED)

include_directories (${CMAKE_CURRENT_BINARY_DIR})

set (CSTR_SRC cstr.c)
add_library (${CSTR_LIB_NAME} ${CSTR_LIB_TYPE} ${CSTR_SRC})

add_executable (main main.c)
target_link_libraries (main ${CSTR_LIB_NAME})

add_executable (benchmark benchmark.c)
target_link_libraries (benchmark ${CSTR_LIB_NAME})

set (CMAKE_C_FLAGS "-std=c99 -Wall -Werror -g -D_GNU_SOURCE")
