cmake_minimum_required(VERSION 3.22)
project(Nebula VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
option(NEBULA_BUILD_EDITOR "Build the Qt editor feasibility prototype" ON)

add_library(nebula_core src/core/document.cpp)
target_include_directories(nebula_core PUBLIC include)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(nebula_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE nebula_core)
    add_test(NAME core COMMAND core_tests)
endif()

if(NEBULA_BUILD_EDITOR)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
    add_library(nebula_editor
        src/editor/page_editor.cpp
        src/editor/block_widgets.cpp
        include/nebula/editor/page_editor.hpp
        include/nebula/editor/block_widgets.hpp)
    set_target_properties(nebula_editor PROPERTIES AUTOMOC ON)
    target_link_libraries(nebula_editor PUBLIC nebula_core Qt6::Widgets)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(nebula_editor PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(nebula_prototype src/prototype/main.cpp)
    target_link_libraries(nebula_prototype PRIVATE nebula_editor)

    if(BUILD_TESTING)
        find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
        add_executable(editor_tests tests/editor_tests.cpp)
        set_target_properties(editor_tests PROPERTIES AUTOMOC ON)
        target_link_libraries(editor_tests PRIVATE nebula_editor Qt6::Test)
        add_test(NAME editor COMMAND editor_tests)
        set_tests_properties(editor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
        add_test(NAME prototype_smoke COMMAND nebula_prototype --smoke-test)
        set_tests_properties(prototype_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
    endif()
endif()
