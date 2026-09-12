get_filename_component(FOOTILLA_SCINTILLA_DIR "${CMAKE_CURRENT_LIST_DIR}/../Scintilla" ABSOLUTE)

# Mirrors win32/scintilla.mak's static COMPONENT_OBJS. Deliberately excludes
# ScintillaDLL.cxx (DllMain) and ScintRes.rc (DLL version resources).
set(_footilla_engine_sources
    AutoComplete CallTip CaseConvert CaseFolder CellBuffer ChangeHistory
    CharacterCategoryMap CharacterType CharClassify ContractionState DBCS
    Decoration Document EditModel Editor EditView Geometry Indicator KeyMap
    LineMarker MarginView PerLine PositionCache RESearch RunStyles Selection
    Style UndoHistory UniConversion UniqueString ViewStyle XPM ScintillaBase)
set(_footilla_platform_sources
    HanjaDic PlatWin ListBox SurfaceGDI SurfaceD2D ScintillaWin)
set(_footilla_sources)
foreach(_source IN LISTS _footilla_engine_sources)
    list(APPEND _footilla_sources "${FOOTILLA_SCINTILLA_DIR}/src/${_source}.cxx")
endforeach()
foreach(_source IN LISTS _footilla_platform_sources)
    list(APPEND _footilla_sources "${FOOTILLA_SCINTILLA_DIR}/win32/${_source}.cxx")
endforeach()
foreach(_source IN LISTS _footilla_sources)
    if(NOT EXISTS "${_source}")
        message(FATAL_ERROR "Incomplete vendored Scintilla source tree: missing '${_source}'.")
    endif()
endforeach()

add_library(footilla_scintilla OBJECT ${_footilla_sources})
target_compile_features(footilla_scintilla PRIVATE cxx_std_17)
target_compile_options(footilla_scintilla PRIVATE /W3 /utf-8 /EHsc /MP)
target_compile_definitions(footilla_scintilla PRIVATE UNICODE _UNICODE)
target_include_directories(footilla_scintilla PRIVATE
    "${FOOTILLA_SCINTILLA_DIR}/include" "${FOOTILLA_SCINTILLA_DIR}/src")

# Preserve the upstream redistribution notice alongside generated binaries.
configure_file("${FOOTILLA_SCINTILLA_DIR}/License.txt"
    "${CMAKE_CURRENT_BINARY_DIR}/Scintilla-License.txt" COPYONLY)
