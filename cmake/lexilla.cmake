# Lexilla (Scintilla's lexer library) vendoring — builds a static `lexilla`
# target, the streaming GUI-independent C++ lexer engine that powers syntax
# coloring.
#
# Lexilla is fetched from GitHub (codeload is reachable from CI runners). It
# needs a handful of Scintilla 5+ headers (ILexer.h, Scintilla.h,
# Sci_Position.h, ...) which it does NOT bundle and which have no GitHub mirror
# — those are VENDORED in-tree under third_party/scintilla/include (Scintilla
# 5.6.3 headers, HPND-licensed; see the adjacent License.txt). No Scintilla
# source is ever compiled.
#
# NOTE: we deliberately do NOT fetch from scintilla.org — CI runners time out
# connecting to it. Lexilla 5.5.0 (tag rel-5-5-0), pinned by SHA256; bump the
# tag + hash to update.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
# Download-only populate (the archive carries build files we don't use).
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
    URL      https://github.com/ScintillaOrg/lexilla/archive/refs/tags/rel-5-5-0.tar.gz
    URL_HASH SHA256=7dd4b7f483af871347f4c4abd11b4671c68dcb9941d29e38f1455610bc758ee8
)
FetchContent_GetProperties(lexilla)
if(NOT lexilla_POPULATED)
    FetchContent_Populate(lexilla)
endif()

# Locate the Lexilla root by finding src/Lexilla.cxx, independent of the archive
# layout (the GitHub tag tarball nests everything under lexilla-rel-<ver>/).
file(GLOB_RECURSE _lexilla_catalogue "${lexilla_SOURCE_DIR}/Lexilla.cxx")
if(NOT _lexilla_catalogue)
    message(FATAL_ERROR "Lexilla: src/Lexilla.cxx not found under ${lexilla_SOURCE_DIR}")
endif()
list(GET _lexilla_catalogue 0 _lexilla_catalogue0)
get_filename_component(_lexilla_src "${_lexilla_catalogue0}" DIRECTORY)   # .../src
get_filename_component(LEXILLA_ROOT "${_lexilla_src}" DIRECTORY)          # Lexilla root

set(SCINTILLA_INCLUDE "${CMAKE_SOURCE_DIR}/third_party/scintilla/include")
if(NOT EXISTS "${SCINTILLA_INCLUDE}/ILexer.h")
    message(FATAL_ERROR "Vendored Scintilla headers missing at ${SCINTILLA_INCLUDE}")
endif()

# All lexers compile in; the catalogue (src/Lexilla.cxx) references each by
# symbol so a static lib pulls them all without /WHOLEARCHIVE.
file(GLOB _lexilla_lexlib CONFIGURE_DEPENDS "${LEXILLA_ROOT}/lexlib/*.cxx")
file(GLOB _lexilla_lexers CONFIGURE_DEPENDS "${LEXILLA_ROOT}/lexers/*.cxx")

add_library(lexilla STATIC
    "${LEXILLA_ROOT}/src/Lexilla.cxx"
    ${_lexilla_lexlib}
    ${_lexilla_lexers}
)
target_include_directories(lexilla
    PUBLIC
        "${SCINTILLA_INCLUDE}"        # ILexer.h, Scintilla.h, Sci_Position.h (vendored)
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
