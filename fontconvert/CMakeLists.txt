
cmake_minimum_required(VERSION 3.15)
project(PolyFontConvert)
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
install(TARGETS fontconvert DESTINATION bin)
