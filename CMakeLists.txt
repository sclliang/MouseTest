cmake_minimum_required(VERSION 3.24)
project(MouseTest VERSION 1.3.10.17 LANGUAGES CXX RC)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "MouseTest requires Windows, MSVC and x64.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(FTC_FLUENTUI_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../FluentUI/sdk" CACHE PATH "Native FluentUI SDK")
find_package(FluentUI 0.2 CONFIG REQUIRED HINTS "${FTC_FLUENTUI_SDK_DIR}")
include(CTest)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHsc;/permissive-;/utf-8;/guard:cf>")

add_library(ModuleConfig STATIC src/modules/ModuleConfig.cpp)
target_include_directories(ModuleConfig PUBLIC src)
target_link_libraries(ModuleConfig PUBLIC bcrypt)

add_library(TouchPadCore STATIC
    src/modules/touch_pad/TouchPadConfig.cpp src/modules/touch_pad/TouchPadConfig.h
    src/modules/touch_pad/TouchPadLogic.cpp src/modules/touch_pad/TouchPadLogic.h)
target_include_directories(TouchPadCore PUBLIC src)
target_link_libraries(TouchPadCore PUBLIC ModuleConfig)

add_library(TouchPadWindow STATIC
    src/modules/touch_pad/TouchPadRawInput.cpp src/modules/touch_pad/TouchPadRawInput.h
    src/modules/touch_pad/TouchPadWindow.cpp src/modules/touch_pad/TouchPadWindow.h)
target_include_directories(TouchPadWindow PUBLIC src)
target_link_libraries(TouchPadWindow PUBLIC TouchPadCore PRIVATE fluent::FluentUI d2d1 dwrite hid user32)

add_library(MouseCore STATIC
    src/modules/mouse/MouseConfig.cpp src/modules/mouse/MouseConfig.h
    src/modules/mouse/MouseLogic.cpp src/modules/mouse/MouseLogic.h)
target_include_directories(MouseCore PUBLIC src)
target_link_libraries(MouseCore PUBLIC ModuleConfig)

add_library(MouseWindow STATIC
    src/modules/mouse/MouseRawInput.cpp src/modules/mouse/MouseRawInput.h
    src/modules/mouse/MouseWindow.cpp src/modules/mouse/MouseWindow.h)
target_include_directories(MouseWindow PUBLIC src)
target_link_libraries(MouseWindow PUBLIC MouseCore PRIVATE fluent::FluentUI d2d1 dwrite hid user32)

configure_file(resources/MouseTest.rc.in generated/MouseTest.rc @ONLY)
configure_file(resources/MouseTest.manifest.in generated/MouseTest.manifest @ONLY)

add_library(MouseTestApplication STATIC
    src/app/Application.cpp src/app/Application.h
    src/app/ToolConfig.cpp src/app/ToolConfig.h)
target_include_directories(MouseTestApplication PUBLIC src)
target_compile_definitions(MouseTestApplication PRIVATE MOUSETEST_VERSION=L"${PROJECT_VERSION}")
target_link_libraries(MouseTestApplication PUBLIC TouchPadWindow MouseWindow)

add_executable(MouseTest WIN32 src/app/Main.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/MouseTest.rc")
target_include_directories(MouseTest PRIVATE resources "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(MouseTest PRIVATE MouseTestApplication)
target_link_options(MouseTest PRIVATE /MANIFEST:NO /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /guard:cf)

if(BUILD_TESTING)
    add_executable(MouseTestConfigTests tests/TouchPadConfigTests.cpp)
    target_link_libraries(MouseTestConfigTests PRIVATE TouchPadCore)
    add_test(NAME MouseTestConfigTests COMMAND MouseTestConfigTests)

    add_executable(MouseTestLogicTests tests/TouchPadLogicTests.cpp)
    target_link_libraries(MouseTestLogicTests PRIVATE TouchPadCore)
    add_test(NAME MouseTestLogicTests COMMAND MouseTestLogicTests)

    add_executable(MouseTestWindowTests tests/TouchPadWindowTests.cpp)
    target_link_libraries(MouseTestWindowTests PRIVATE TouchPadWindow)
    add_test(NAME MouseTestWindowTests COMMAND MouseTestWindowTests)
    set_tests_properties(MouseTestWindowTests PROPERTIES TIMEOUT 30)

    add_executable(MouseTestMouseConfigTests tests/MouseConfigTests.cpp)
    target_link_libraries(MouseTestMouseConfigTests PRIVATE MouseCore)
    add_test(NAME MouseTestMouseConfigTests COMMAND MouseTestMouseConfigTests)

    add_executable(MouseTestMouseLogicTests tests/MouseLogicTests.cpp)
    target_link_libraries(MouseTestMouseLogicTests PRIVATE MouseCore)
    add_test(NAME MouseTestMouseLogicTests COMMAND MouseTestMouseLogicTests)

    add_executable(MouseTestMouseWindowTests tests/MouseWindowTests.cpp)
    target_link_libraries(MouseTestMouseWindowTests PRIVATE MouseWindow)
    add_test(NAME MouseTestMouseWindowTests COMMAND MouseTestMouseWindowTests)
    set_tests_properties(MouseTestMouseWindowTests PROPERTIES TIMEOUT 30)

    add_executable(MouseTestToolConfigTests tests/ToolConfigTests.cpp)
    target_link_libraries(MouseTestToolConfigTests PRIVATE MouseTestApplication)
    add_test(NAME MouseTestToolConfigTests COMMAND MouseTestToolConfigTests)

    # Manual visual fixture: shows the ink canvas and drives a synthetic path.
    # It moves the desktop cursor and is intentionally not registered with CTest.
    add_executable(MouseInkVisual tests/MouseInkVisual.cpp)
    target_link_libraries(MouseInkVisual PRIVATE MouseWindow)
endif()
