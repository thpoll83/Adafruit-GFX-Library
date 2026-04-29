
cmake_minimum_required(VERSION 3.15)
project(PolyFontConvert)

include(GNUInstallDirs)

# Default install prefix — avoid needing sudo on Linux, use per-user app dir on Windows.
# Override at configure time:  cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
# Override at install time:    cmake --install . --prefix /usr/local
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT OR
   CMAKE_INSTALL_PREFIX STREQUAL "/usr/local")
  if(WIN32)
    set(CMAKE_INSTALL_PREFIX "$ENV{LOCALAPPDATA}/fontconvert" CACHE PATH
        "Install prefix (default: %LOCALAPPDATA%\\fontconvert)" FORCE)
  else()
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH
        "Install prefix (default: ~/.local)" FORCE)
  endif()
endif()

# On Windows, bundle the getopt shim; POSIX provides its own.
if(WIN32)
  add_executable(fontconvert
    src/fontconvert.c
    src/cli.c
    src/dither.c
    src/font_render.c
    src/getopt_win.c)
else()
  add_executable(fontconvert
    src/fontconvert.c
    src/cli.c
    src/dither.c
    src/font_render.c)
endif()

# Include the subdirectory containing freetype_with_harfbuzz
add_subdirectory(freetype-hb)

# Add your executable and link the necessary libraries
add_dependencies(fontconvert freetype_with_harfbuzz)

ADD_LIBRARY(freetype STATIC IMPORTED)
ADD_LIBRARY(hb STATIC IMPORTED)

# FREETYPE_DIR, FREETYPE_LIB_NAME, HARFBUZZ_DIR, HARFBUZZ_LIB_NAME are set
# by freetype-hb/CMakeLists.txt via PARENT_SCOPE.
SET_TARGET_PROPERTIES(freetype PROPERTIES
    IMPORTED_LOCATION ${FREETYPE_DIR}/lib/${FREETYPE_LIB_NAME})
SET_TARGET_PROPERTIES(hb PROPERTIES
    IMPORTED_LOCATION ${HARFBUZZ_DIR}/lib/${HARFBUZZ_LIB_NAME})

# libpng is required by FreeType (built with FT_REQUIRE_PNG=TRUE) and linked
# directly by fontconvert. On Windows, provide it via vcpkg or set PNG_ROOT.
find_package(PNG REQUIRED)

TARGET_LINK_LIBRARIES(fontconvert freetype)
TARGET_LINK_LIBRARIES(fontconvert hb)
TARGET_LINK_LIBRARIES(fontconvert PNG::PNG)

target_include_directories(fontconvert PRIVATE ${FREETYPE_DIR}/include/freetype2)
target_include_directories(fontconvert PRIVATE ${HARFBUZZ_DIR}/include/harfbuzz)

# MSVC-specific options
if(MSVC)
  target_compile_options(fontconvert PRIVATE /utf-8 /W3)
  target_compile_definitions(fontconvert PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Install the executable
install(TARGETS fontconvert
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  fontconvert will be installed to: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
message(STATUS "  Install with:  cmake --install . [--prefix /usr/local]")
