cmake_minimum_required(VERSION 3.6)
project(SSFS)

set(CMAKE_C_STANDARD 99)

set(SOURCE_FILES sfs_api.c disk_emu.c)
set(SOURCES_TEST1 sfs_test1.c tests.c)

add_executable(SSFS ${SOURCE_FILES})
add_executable(test1 ${SOURCE_FILES} ${SOURCES_TEST1})