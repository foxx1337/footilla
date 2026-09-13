add_library(
    foo_tilla SHARED
    main.cpp
    preferences.cpp
    resource.h
    foo_tilla.rc
)

target_link_libraries(
    foo_tilla
    PRIVATE foobar2000 wtl footilla::footilla Shlwapi.lib Uxtheme.lib
)

add_custom_command(
    TARGET foo_tilla POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../Scintilla/License.txt"
        "$<TARGET_FILE_DIR:foo_tilla>/Scintilla-License.txt"
    VERBATIM
)

source_group("Resource Files" foo_tilla.rc)

# Link statically, the foobar2000 sdk doesn't do it
#[[
set_property(
    TARGET foo_nowplaying2 PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
]]
