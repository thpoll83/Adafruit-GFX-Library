
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
# as a transitive static dependency.  On Windows, PNG_EP_DIR / ZLIB_EP_DIR are
# set by freetype-hb/CMakeLists.txt (ExternalProject builds); on other
# platforms use the system-provided libpng.
if(WIN32 AND DEFINED PNG_EP_DIR)
  add_library(PNG::PNG STATIC IMPORTED)
  set_target_properties(PNG::PNG PROPERTIES
    IMPORTED_LOCATION "${PNG_EP_DIR}/lib/${PNG_EP_LIB_NAME}")
  add_library(ZLIB_ep STATIC IMPORTED)
  set_target_properties(ZLIB_ep PROPERTIES
    IMPORTED_LOCATION "${ZLIB_EP_DIR}/lib/${ZLIB_EP_LIB_NAME}")
else()
  find_package(PNG REQUIRED)
endif()

TARGET_LINK_LIBRARIES(fontconvert freetype)
TARGET_LINK_LIBRARIES(fontconvert hb)
TARGET_LINK_LIBRARIES(fontconvert PNG::PNG)
if(WIN32 AND DEFINED ZLIB_EP_DIR)
  TARGET_LINK_LIBRARIES(fontconvert ZLIB_ep)
endif()
if(NOT MSVC)
  TARGET_LINK_LIBRARIES(fontconvert m)
endif()

target_include_directories(fontconvert PRIVATE ${FREETYPE_DIR}/include/freetype2)
target_include_directories(fontconvert PRIVATE ${HARFBUZZ_DIR}/include/harfbuzz)
if(WIN32 AND DEFINED PNG_EP_DIR)
  target_include_directories(fontconvert PRIVATE "${PNG_EP_DIR}/include")
endif()

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
