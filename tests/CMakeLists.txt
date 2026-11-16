# unit tests, enabled with -DCSGO_GC_BUILD_TESTS=ON, run with ctest

find_package(Protobuf CONFIG REQUIRED)
find_package(libwebsockets CONFIG REQUIRED)

# tests run in a copy of the fixture since the gc writes files (inventory.txt) next to it
set(FIXTURE_DIR ${CMAKE_CURRENT_BINARY_DIR}/fixture)
file(GLOB_RECURSE FIXTURE_FILES CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/fixture
    ${CMAKE_CURRENT_SOURCE_DIR}/fixture/*)
foreach(file ${FIXTURE_FILES})
    configure_file(fixture/${file} ${FIXTURE_DIR}/${file} COPYONLY)
endforeach()

function(add_gc_test name)
    add_executable(${name} ${name}.cpp)

    # some tests include csgo_gc sources directly, so they need the same include paths
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/csgo_gc
        ${CMAKE_BINARY_DIR}/csgo_gc/generated)

    target_link_libraries(${name} PRIVATE
        csgo_gc
        protobuf::libprotobuf-lite
        websockets)

    if (WIN32)
        # libuv (pulled in by libwebsockets) needs this when linking an executable
        target_link_libraries(${name} PRIVATE dbghelp)
    endif()

    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${FIXTURE_DIR})

    # tests share the fixture's inventory.txt, so never run them at the same time
    set_tests_properties(${name} PROPERTIES RESOURCE_LOCK fixture)
endfunction()

add_gc_test(editor_protocol_test)
add_gc_test(prestige_test)
add_gc_test(items_test)
add_gc_test(ui_patches_test)
