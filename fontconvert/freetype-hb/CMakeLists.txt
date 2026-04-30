cmake_minimum_required(VERSION 3.15)
project(freetype-with-harfbuzz)
include(ExternalProject)

# Shorten ExternalProject output paths to avoid Windows MAX_PATH (260 char) limit.
# All EP Source/Build/Install dirs land under cmake-build-debug/freetype-hb/ep/<name>/
set_property(DIRECTORY PROPERTY EP_BASE "${CMAKE_CURRENT_BINARY_DIR}/ep")

# -------------------------------------------------------
# Lib name conventions
# On Windows with the VS multi-config generator CMAKE_BUILD_TYPE is not set,
# so we always build external deps in Release and use the Release lib names.
# -------------------------------------------------------

if (WIN32)
  set(freetype_lib_name freetype.lib)
  set(harfbuzz_lib_name harfbuzz.lib)
  set(zlib_lib_name     zlibstatic.lib)
  set(png_lib_name      libpng16_static.lib)
else()
  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(freetype_lib_name libfreetyped.a)
  else()
    set(freetype_lib_name libfreetype.a)
  endif()
  set(harfbuzz_lib_name libharfbuzz.a)
endif()

# -------------------------------------------------------
# Step 0 (Windows only): Build zlib + libpng from source.
# On Linux/macOS these come from the system package manager.
# -------------------------------------------------------

if (WIN32)

  # Force Release so lib names are predictable across Debug/Release outer builds.
  set(win32_release_build
    BUILD_COMMAND   ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config Release
  )

  ExternalProject_Add(
    zlib
    URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DBUILD_SHARED_LIBS=OFF
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/${zlib_lib_name}
    ${win32_release_build}
  )
  ExternalProject_Get_Property(zlib install_dir)
  set(zlib_ep_dir ${install_dir})
  set(ZLIB_EP_DIR     ${install_dir}     PARENT_SCOPE)
  set(ZLIB_EP_LIB_NAME ${zlib_lib_name} PARENT_SCOPE)

  ExternalProject_Add(
    libpng
    DEPENDS zlib
    URL https://download.sourceforge.net/libpng/libpng-1.6.43.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DPNG_SHARED=OFF
      -DPNG_STATIC=ON
      -DPNG_TESTS=OFF
      -DZLIB_LIBRARY=${zlib_ep_dir}/lib/${zlib_lib_name}
      -DZLIB_INCLUDE_DIR=${zlib_ep_dir}/include
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/${png_lib_name}
    ${win32_release_build}
  )
  ExternalProject_Get_Property(libpng install_dir)
  set(png_ep_dir ${install_dir})
  set(PNG_EP_DIR      ${install_dir}    PARENT_SCOPE)
  set(PNG_EP_LIB_NAME ${png_lib_name}  PARENT_SCOPE)

  set(win32_extra_deps      zlib libpng)
  set(win32_ft_extra_prefix ";${zlib_ep_dir};${png_ep_dir}")
  set(win32_ft_png_args
    -DPNG_LIBRARY=${png_ep_dir}/lib/${png_lib_name}
    -DPNG_PNG_INCLUDE_DIR=${png_ep_dir}/include
    -DZLIB_LIBRARY=${zlib_ep_dir}/lib/${zlib_lib_name}
    -DZLIB_INCLUDE_DIR=${zlib_ep_dir}/include
  )

else()
  set(win32_extra_deps      "")
  set(win32_ft_extra_prefix "")
  set(win32_ft_png_args     "")
  set(win32_release_build   "")
endif()


# -------------------------------------------------------
# Step 1: Build FreeType w/o Harfbuzz
# -------------------------------------------------------

list(APPEND freetype_byproduct
  <INSTALL_DIR>/lib/${freetype_lib_name}
)

ExternalProject_Add(
  freetype_without_harfbuzz
  URL https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_BUILD_TYPE=Release
    -DFT_DISABLE_HARFBUZZ=TRUE
    -DFT_DISABLE_ZLIB=TRUE
    -DFT_DISABLE_PNG=TRUE
  BUILD_BYPRODUCTS ${freetype_byproduct}
  ${win32_release_build}
)

ExternalProject_Get_Property(freetype_without_harfbuzz install_dir)
set(freetype_without_harfbuzz_dir ${install_dir})


# -------------------------------------------------------
# Step 2: Build Harfbuzz with FreeType
# -------------------------------------------------------

# On Windows use the CMake build. On Linux use autotools (avoids a symbol
# conflict that appears with the HarfBuzz CMake build on some distros).

if (WIN32)

  ExternalProject_Add(
    harfbuzz
    DEPENDS freetype_without_harfbuzz
    URL https://www.freedesktop.org/software/harfbuzz/release/harfbuzz-2.6.7.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_PREFIX_PATH=${freetype_without_harfbuzz_dir}
      -DHB_HAVE_FREETYPE=On
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/${harfbuzz_lib_name}
    ${win32_release_build}
  )

else()

  ExternalProject_Add(
    harfbuzz
    DEPENDS freetype_without_harfbuzz
    URL https://www.freedesktop.org/software/harfbuzz/release/harfbuzz-2.6.7.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    CONFIGURE_COMMAND
      FREETYPE_CFLAGS="-I${freetype_without_harfbuzz_dir}/include/freetype2/"
      FREETYPE_LIBS="${freetype_without_harfbuzz_dir}/lib/${freetype_lib_name}"
      <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --enable-static=yes --enable-shared=no
    BUILD_COMMAND   make -C src libharfbuzz.la
    INSTALL_COMMAND make -C src install
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/${harfbuzz_lib_name}
  )

endif()

ExternalProject_Get_Property(harfbuzz install_dir)
set(harfbuzz_dir ${install_dir})
set(HARFBUZZ_DIR     ${harfbuzz_dir}     PARENT_SCOPE)
set(HARFBUZZ_LIB_NAME ${harfbuzz_lib_name} PARENT_SCOPE)


# -------------------------------------------------------
# Step 3: Build FreeType with Harfbuzz (and PNG on Windows)
# -------------------------------------------------------

ExternalProject_Add(
  freetype_with_harfbuzz
  DEPENDS harfbuzz ${win32_extra_deps}
  URL https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_PREFIX_PATH=${harfbuzz_dir}${win32_ft_extra_prefix}
    -DCMAKE_MODULE_PATH=${harfbuzz_dir}
    -DFT_REQUIRE_HARFBUZZ=TRUE
    -DFT_REQUIRE_PNG=TRUE
    ${win32_ft_png_args}
  BUILD_BYPRODUCTS ${freetype_byproduct}
  ${win32_release_build}
)

ExternalProject_Get_Property(freetype_with_harfbuzz install_dir)
set(FREETYPE_DIR      ${install_dir}       PARENT_SCOPE)
set(FREETYPE_LIB_NAME ${freetype_lib_name} PARENT_SCOPE)

list(APPEND deps freetype_with_harfbuzz)

# -------------------------------------------------------

add_executable(test test.cpp)
add_dependencies(test ${deps})

# -------------------------------------------------------
