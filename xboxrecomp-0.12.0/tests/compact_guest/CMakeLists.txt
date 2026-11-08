cmake_minimum_required(VERSION 3.20)
project(compact_guest_test C)
# RECOMP_COMPACT_GUEST: the table-mapped guest address space for 32-bit /
# non-x86 hosts. Builds the runtime with the option forced on.
set(CMAKE_C_STANDARD 11)
set(XBOXRECOMP_COMPACT_GUEST ON CACHE BOOL "" FORCE)
add_subdirectory(../.. runtime)
add_executable(compact_guest_test test_main.c)
target_include_directories(compact_guest_test PRIVATE
    ../../src/kernel ../../src ../../templates/runtime)
target_link_libraries(compact_guest_test PRIVATE xboxrecomp)
enable_testing()
add_test(NAME compact_guest_test
         COMMAND compact_guest_test
                 ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/conformance/test.xbe)
