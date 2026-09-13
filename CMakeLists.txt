cmake_minimum_required(VERSION 3.24)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

project(footilla VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /DUNICODE /D_UNICODE /DNOMINMAX")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /DUNICODE /D_UNICODE /DNOMINMAX")

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Footilla currently requires Windows and an MSVC-compatible toolchain.")
endif()

option(FOOTILLA_BUILD_DEMO "Build the WTL editor playground" ${PROJECT_IS_TOP_LEVEL})
option(FOOTILLA_BUILD_TESTS "Build the language and native control tests" ${PROJECT_IS_TOP_LEVEL})
option(FOOTILLA_BUILD_FOO_TILLA "Build the foobar2000 sample plugin" ${PROJECT_IS_TOP_LEVEL})

if(CMAKE_GENERATOR_PLATFORM)
    set(platform ${CMAKE_GENERATOR_PLATFORM})
else()
    set(platform ${CMAKE_VS_PLATFORM_NAME_DEFAULT})
endif()
string(TOUPPER "${platform}" uplatform)

if(${platform} STREQUAL "Win32")
    set(platform_DIR "")
else()
    set(platform_DIR "${platform}/")
endif()

add_compile_options(/W4 /permissive- /utf-8 /EHsc)
#add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/W4>)
#add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/MP>)
#add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/permissive->)
#add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
#add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/EHsc>)

include(cmake/Scintilla.cmake)

# Include the engine's object files in footilla.lib, not a second static library.
add_library(footilla STATIC src/editor.cpp src/language.cpp $<TARGET_OBJECTS:footilla_scintilla>)
add_library(footilla::footilla ALIAS footilla)
#target_compile_features(footilla PUBLIC cxx_std_17)
target_include_directories(footilla PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_include_directories(footilla PRIVATE "${FOOTILLA_SCINTILLA_DIR}/include")
target_link_libraries(footilla PUBLIC
    user32 gdi32 imm32 ole32 oleaut32 advapi32 comctl32)
add_custom_command(TARGET footilla POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/Scintilla-License.txt"
        "$<TARGET_FILE_DIR:footilla>/Scintilla-License.txt"
    VERBATIM)

if(FOOTILLA_BUILD_DEMO)
    find_package(wtl)
    find_path(WTL_INCLUDE_DIR atlapp.h
        HINTS "${wtl}/Include" "${FOOTILLA_WTL_DIR}")
    if(NOT WTL_INCLUDE_DIR)
        message(FATAL_ERROR "WTL not found. Set wtl to the WTL root, or disable FOOTILLA_BUILD_DEMO.")
    endif()
    add_executable(footilla_demo WIN32 demo/main.cpp demo/demo.manifest)
    target_compile_definitions(footilla_demo PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_link_libraries(footilla_demo PRIVATE wtl footilla::footilla)
    if(EXISTS "${WTL_INCLUDE_DIR}/../MS-PL.txt")
        add_custom_command(TARGET footilla_demo POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${WTL_INCLUDE_DIR}/../MS-PL.txt"
                "$<TARGET_FILE_DIR:footilla_demo>/WTL-License.txt"
            VERBATIM)
    endif()
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT footilla_demo)
endif()

if(FOOTILLA_BUILD_TESTS)
    enable_testing()
    add_executable(footilla_language_tests tests/language_tests.cpp src/language.cpp)
    target_include_directories(footilla_language_tests PRIVATE include)
    set_property(TARGET footilla_language_tests PROPERTY CXX_STANDARD 17) # needed for u8 std::string / literals
    add_test(NAME footilla.language COMMAND footilla_language_tests)

    add_executable(footilla_control_tests tests/control_tests.cpp)
    target_include_directories(footilla_control_tests PRIVATE "${FOOTILLA_SCINTILLA_DIR}/include")
    set_property(TARGET footilla_control_tests PROPERTY CXX_STANDARD 17) # needed for u8 std::string / literals
    target_link_libraries(footilla_control_tests PRIVATE footilla::footilla)
    add_test(NAME footilla.control COMMAND footilla_control_tests)

    add_executable(footilla_visual_tests tests/visual_tests.cpp)
    target_include_directories(footilla_visual_tests PRIVATE "${FOOTILLA_SCINTILLA_DIR}/include")
    set_property(TARGET footilla_visual_tests PROPERTY CXX_STANDARD 17) # needed for u8 std::string / literals
    target_link_libraries(footilla_visual_tests PRIVATE footilla::footilla)
    add_test(NAME footilla.visual COMMAND footilla_visual_tests)
endif()

if(FOOTILLA_BUILD_FOO_TILLA)
    find_package(foobar2000)
    if(NOT WTL_INCLUDE_DIR)
        find_package(wtl)
    endif()
    add_subdirectory(foo_tilla)
endif()
