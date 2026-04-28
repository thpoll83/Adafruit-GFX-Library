cmake_minimum_required(VERSION 3.15)
project(freetype-with-harfbuzz)
include(ExternalProject)

# Set the name of the "byproduct" which is necessary when using
# the Ninja generator and External Project dependencies.
# -------------------------------------------------------

if (WIN32)
  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(freetype_lib_name freetyped.lib)
  else()
    set(freetype_lib_name freetype.lib)
  endif()
  set(harfbuzz_lib_name harfbuzz.lib)
else()
  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(freetype_lib_name libfreetyped.a)
  else()
    set(freetype_lib_name libfreetype.a)
  endif()
  set(harfbuzz_lib_name libharfbuzz.a)
endif()

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
  -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
  -DFT_DISABLE_HARFBUZZ=TRUE
  -DFT_DISABLE_ZLIB=TRUE
  BUILD_BYPRODUCTS ${freetype_byproduct}
  )

ExternalProject_Get_Property(freetype_without_harfbuzz install_dir)
set(freetype_without_harfbuzz_dir ${install_dir})


# Step 2: Build Harfbuzz with FreeType
# -------------------------------------------------------

# When I use the CMake build solution for HarfBuzz I run into
# an regarding some symbols. The autotools build process
# works fine, so that's what I'm using on Linux.

if (WIN32)

  ExternalProject_Add(
    harfbuzz
    DEPENDS freetype_without_harfbuzz
    URL https://www.freedesktop.org/software/harfbuzz/release/harfbuzz-2.6.7.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_PREFIX_PATH=${freetype_without_harfbuzz_dir}
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DHB_HAVE_FREETYPE=On
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/${harfbuzz_lib_name}
  )
  
else()
  
  ExternalProject_Add(
    harfbuzz
    DEPENDS freetype_without_harfbuzz
    URL https://www.freedesktop.org/software/harfbuzz/release/harfbuzz-2.6.7.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    CONFIGURE_COMMAND FREETYPE_CFLAGS="-I${freetype_without_harfbuzz_dir}/include/freetype2/" FREETYPE_LIBS="${freetype_without_harfbuzz_dir}/lib/${freetype_lib_name}" <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --enable-static=yes --enable-shared=no
    BUILD_COMMAND make
    INSTALL_COMMAND make install
  )

endif()

ExternalProject_Get_Property(harfbuzz install_dir)
set(harfbuzz_dir ${install_dir})
set(HARFBUZZ_DIR ${harfbuzz_dir} PARENT_SCOPE)
set(HARFBUZZ_LIB_NAME ${harfbuzz_lib_name} PARENT_SCOPE)

# Step 3: Build FreeType with Harfbuzz
# -------------------------------------------------------

ExternalProject_Add(
  freetype_with_harfbuzz
  DEPENDS harfbuzz
  URL https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  CMAKE_ARGS
  -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
  -DCMAKE_PREFIX_PATH=${harfbuzz_dir}
  -DCMAKE_MODULE_PATH=${harfbuzz_dir}
  -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
  -DFT_REQUIRE_HARFBUZZ=TRUE
  -DFT_REQUIRE_PNG=TRUE
  BUILD_BYPRODUCTS ${freetype_byproduct}
  )

ExternalProject_Get_Property(freetype_with_harfbuzz install_dir)
set(FREETYPE_DIR ${install_dir} PARENT_SCOPE)
set(FREETYPE_LIB_NAME ${freetype_lib_name} PARENT_SCOPE)

list(APPEND deps freetype_with_harfbuzz)

# -------------------------------------------------------

add_executable(test test.cpp)
add_dependencies(test ${deps})

# -------------------------------------------------------
