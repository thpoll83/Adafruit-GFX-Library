# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/Source/freetype_with_harfbuzz"
  "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/Build/freetype_with_harfbuzz"
  "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/Install/freetype_with_harfbuzz"
  "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/tmp/freetype_with_harfbuzz"
  "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/Stamp/freetype_with_harfbuzz"
  "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/Download/freetype_with_harfbuzz"
  "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/Stamp/freetype_with_harfbuzz"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/Stamp/freetype_with_harfbuzz/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/Stamp/freetype_with_harfbuzz${cfgdir}") # cfgdir has leading slash
endif()
