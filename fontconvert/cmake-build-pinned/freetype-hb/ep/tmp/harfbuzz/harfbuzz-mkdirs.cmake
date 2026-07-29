# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/Source/harfbuzz"
  "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/Build/harfbuzz"
  "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/Install/harfbuzz"
  "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/tmp/harfbuzz"
  "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/Stamp/harfbuzz"
  "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/Download/harfbuzz"
  "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/Stamp/harfbuzz"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/Stamp/harfbuzz/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/user/Adafruit-GFX-Library/fontconvert/cmake-build-pinned/freetype-hb/ep/Stamp/harfbuzz${cfgdir}") # cfgdir has leading slash
endif()
