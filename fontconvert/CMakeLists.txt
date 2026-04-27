
cmake_minimum_required(VERSION 3.15)
project(PolyFontConvert)

include(GNUInstallDirs)

# Default install prefix to ~/.local (avoids needing sudo for a personal tool).
# Override at configure time:  cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
# Override at install time:    cmake --install . --prefix /usr/local
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT OR
   CMAKE_INSTALL_PREFIX STREQUAL "/usr/local")
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH
        "Install prefix (default: ~/.local)" FORCE)
endif()

add_executable(fontconvert src/fontconvert.c)

# Include the subdirectory containing freetype_with_harfbuzz
add_subdirectory(freetype-hb)

# Add your executable and link the necessary libraries
add_dependencies(fontconvert freetype_with_harfbuzz)

ADD_LIBRARY(freetype STATIC IMPORTED)
ADD_LIBRARY(hb STATIC IMPORTED)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    SET_TARGET_PROPERTIES(freetype PROPERTIES IMPORTED_LOCATION ${FREETYPE_DIR}/lib/libfreetyped.a)
else ()
    SET_TARGET_PROPERTIES(freetype PROPERTIES IMPORTED_LOCATION ${FREETYPE_DIR}/lib/libfreetype.a)
    endif()
SET_TARGET_PROPERTIES(hb PROPERTIES IMPORTED_LOCATION ${HARFBUZZ_DIR}/lib/libharfbuzz.a)
TARGET_LINK_LIBRARIES(fontconvert freetype)
TARGET_LINK_LIBRARIES(fontconvert hb)
#link_directories(fontconvert ${FREETYPE_DIR}/lib)
#target_link_libraries(fontconvert freetype)
target_include_directories(fontconvert PRIVATE ${FREETYPE_DIR}/include/freetype2 )
target_include_directories(fontconvert PRIVATE ${HARFBUZZ_DIR}/include/harfbuzz )

# Install the executable
install(TARGETS fontconvert
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  fontconvert will be installed to: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
message(STATUS "  Install with:  cmake --install . [--prefix /usr/local]")
