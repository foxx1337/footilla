cmake_minimum_required(VERSION 3.24)
project(footilla VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Footilla currently requires Windows and an MSVC-compatible toolchain.")
endif()

set(FOOTILLA_SCITE_SOURCE_DIR "" CACHE PATH
    "SciTE source bundle containing scintilla/, or the scintilla source directory itself")
set(FOOTILLA_WTL_DIR "" CACHE PATH "WTL root containing Include/atlapp.h (demo only)")
option(FOOTILLA_BUILD_DEMO "Build the WTL editor playground" ${PROJECT_IS_TOP_LEVEL})
option(FOOTILLA_BUILD_TESTS "Build the language and native control tests" ${PROJECT_IS_TOP_LEVEL})

include(cmake/Scintilla.cmake)

# Include the engine's object files in footilla.lib, not a second static library.
add_library(footilla STATIC src/editor.cpp src/language.cpp $<TARGET_OBJECTS:footilla_scintilla>)
add_library(footilla::footilla ALIAS footilla)
target_compile_features(footilla PUBLIC cxx_std_17)
target_include_directories(footilla PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_include_directories(footilla PRIVATE "${FOOTILLA_SCINTILLA_DIR}/include")
target_compile_definitions(footilla PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(footilla PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(footilla PUBLIC
    user32 gdi32 imm32 ole32 oleaut32 advapi32 comctl32)
add_custom_command(TARGET footilla POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/Scintilla-License.txt"
        "$<TARGET_FILE_DIR:footilla>/Scintilla-License.txt"
    VERBATIM)

if(FOOTILLA_BUILD_DEMO)
    find_path(FOOTILLA_WTL_INCLUDE_DIR atlapp.h
        HINTS "${FOOTILLA_WTL_DIR}/Include" "${FOOTILLA_WTL_DIR}")
    if(NOT FOOTILLA_WTL_INCLUDE_DIR)
        message(FATAL_ERROR "WTL not found. Set FOOTILLA_WTL_DIR to the WTL root, or disable FOOTILLA_BUILD_DEMO.")
    endif()
    add_executable(footilla_demo WIN32 demo/main.cpp demo/demo.manifest)
    target_include_directories(footilla_demo PRIVATE "${FOOTILLA_WTL_INCLUDE_DIR}")
    target_compile_definitions(footilla_demo PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_compile_options(footilla_demo PRIVATE /W4 /utf-8 /EHsc)
    target_link_libraries(footilla_demo PRIVATE footilla::footilla)
    if(EXISTS "${FOOTILLA_WTL_INCLUDE_DIR}/../MS-PL.txt")
        add_custom_command(TARGET footilla_demo POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${FOOTILLA_WTL_INCLUDE_DIR}/../MS-PL.txt"
                "$<TARGET_FILE_DIR:footilla_demo>/WTL-License.txt"
            VERBATIM)
    endif()
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT footilla_demo)
endif()

if(FOOTILLA_BUILD_TESTS)
    enable_testing()
    add_executable(footilla_language_tests tests/language_tests.cpp src/language.cpp)
    target_include_directories(footilla_language_tests PRIVATE include)
    target_compile_features(footilla_language_tests PRIVATE cxx_std_17)
    target_compile_options(footilla_language_tests PRIVATE /W4 /utf-8 /EHsc)
    add_test(NAME footilla.language COMMAND footilla_language_tests)

    add_executable(footilla_control_tests tests/control_tests.cpp)
    target_include_directories(footilla_control_tests PRIVATE "${FOOTILLA_SCINTILLA_DIR}/include")
    target_compile_definitions(footilla_control_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(footilla_control_tests PRIVATE /W4 /utf-8 /EHsc)
    target_link_libraries(footilla_control_tests PRIVATE footilla::footilla)
    add_test(NAME footilla.control COMMAND footilla_control_tests)
endif()
