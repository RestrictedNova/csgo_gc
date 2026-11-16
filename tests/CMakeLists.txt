# unit tests, enabled with -DCSGO_GC_BUILD_TESTS=ON, run with ctest

find_package(Protobuf CONFIG REQUIRED)
find_package(libwebsockets CONFIG REQUIRED)

add_executable(editor_protocol_test editor_protocol_test.cpp)

# the test includes csgo_gc sources directly, so it needs the same include paths
target_include_directories(editor_protocol_test PRIVATE
    ${CMAKE_SOURCE_DIR}/csgo_gc
    ${CMAKE_BINARY_DIR}/csgo_gc/generated)

target_link_libraries(editor_protocol_test PRIVATE
    csgo_gc
    protobuf::libprotobuf-lite
    websockets)

if (WIN32)
    # libuv (pulled in by libwebsockets) needs this when linking an executable
    target_link_libraries(editor_protocol_test PRIVATE dbghelp)
endif()

add_test(NAME editor_protocol_test
    COMMAND editor_protocol_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/fixture)
