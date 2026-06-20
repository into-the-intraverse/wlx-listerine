# Lexilla (Scintilla's lexer library) vendoring — builds a static `lexilla`
# target, the streaming GUI-independent C++ lexer engine that replaces
# tree-sitter for syntax coloring.
#
# Lexilla needs a handful of Scintilla 5+ headers (ILexer.h, Scintilla.h,
# Sci_Position.h, ...) which it does NOT bundle, so we fetch Scintilla too —
# for headers only; no Scintilla source is ever compiled.
#
# Versions: Lexilla 5.5.0, Scintilla 5.6.3.
# Sources come from scintilla.org. The archive filenames are version-encoded
# (lexilla550.zip / scintilla563.zip), so each URL is effectively immutable, and
# the SHA256 pins below guarantee integrity. Scintilla has no GitHub mirror
# (ScintillaOrg/scintilla 404s), so scintilla.org is canonical. If upstream ever
# prunes an old versioned archive, configure fails loudly on the hash/URL — bump
# to the current release + new hash (same failure mode the old grammar fetches
# had).

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
# Download-only populate (no add_subdirectory: the archives carry their own
# build files we don't want). Mirrors cmake/grammars.cmake's policy setup so
# this file stays correct after grammars.cmake is removed.
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
if(POLICY CMP0168)
    cmake_policy(SET CMP0168 NEW)
endif()
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

FetchContent_Declare(lexilla
    URL      https://www.scintilla.org/lexilla550.zip
    URL_HASH SHA256=b165bea71e80b43eff3ae833cad560ceb3c5f1ddf150a2a30c6313eea7e1efee
)
FetchContent_Declare(scintilla
    URL      https://www.scintilla.org/scintilla563.zip
    URL_HASH SHA256=b2eddd6771d7d989854d336673965ba8036d41bf7682d69806262c7acda6d1f4
)

FetchContent_GetProperties(lexilla)
if(NOT lexilla_POPULATED)
    FetchContent_Populate(lexilla)
endif()
FetchContent_GetProperties(scintilla)
if(NOT scintilla_POPULATED)
    FetchContent_Populate(scintilla)
endif()

# The .zip archives contain a single top-level lexilla/ (resp. scintilla/)
# directory which CMake's URL extraction does not strip. Resolve the real root
# defensively so a future archive that drops the nesting still works.
if(EXISTS "${lexilla_SOURCE_DIR}/lexilla/src/Lexilla.cxx")
    set(LEXILLA_ROOT "${lexilla_SOURCE_DIR}/lexilla")
elseif(EXISTS "${lexilla_SOURCE_DIR}/src/Lexilla.cxx")
    set(LEXILLA_ROOT "${lexilla_SOURCE_DIR}")
else()
    message(FATAL_ERROR "Lexilla: src/Lexilla.cxx not found under ${lexilla_SOURCE_DIR}")
endif()

if(EXISTS "${scintilla_SOURCE_DIR}/scintilla/include/ILexer.h")
    set(SCINTILLA_INCLUDE "${scintilla_SOURCE_DIR}/scintilla/include")
elseif(EXISTS "${scintilla_SOURCE_DIR}/include/ILexer.h")
    set(SCINTILLA_INCLUDE "${scintilla_SOURCE_DIR}/include")
else()
    message(FATAL_ERROR "Scintilla: include/ILexer.h not found under ${scintilla_SOURCE_DIR}")
endif()

# All lexers compile in (125 of them) — every Lexilla language becomes available
# for free; we ship style/keyword maps for the subset we support. lexlib holds
# the shared lexer runtime; src/Lexilla.cxx is the catalogue + CreateLexer entry.
file(GLOB _lexilla_lexlib CONFIGURE_DEPENDS "${LEXILLA_ROOT}/lexlib/*.cxx")
file(GLOB _lexilla_lexers CONFIGURE_DEPENDS "${LEXILLA_ROOT}/lexers/*.cxx")

add_library(lexilla STATIC
    "${LEXILLA_ROOT}/src/Lexilla.cxx"
    ${_lexilla_lexlib}
    ${_lexilla_lexers}
)
target_include_directories(lexilla
    PUBLIC
        "${SCINTILLA_INCLUDE}"        # ILexer.h, Scintilla.h, Sci_Position.h
        "${LEXILLA_ROOT}/include"     # Lexilla.h (CreateLexer), SciLexer.h (SCE_*)
    PRIVATE
        "${LEXILLA_ROOT}/lexlib"      # Accessor.h, StyleContext.h, WordList.h, ...
)
set_target_properties(lexilla PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
# Vendored third-party code: silence its warnings (not first-party /W4 /WX) and
# allow strcpy etc. in Lexilla.cxx.
if(MSVC)
    target_compile_options(lexilla PRIVATE /w)
    target_compile_definitions(lexilla PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
