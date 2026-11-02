# Set name of the module
SET (MODULE wl_screen)

# wlr-screencopy is not part of wayland-protocols: the protocol file (MIT) is kept here
find_program(WAYLAND_SCANNER_EXEC wayland-scanner REQUIRED)
SET(SCREENCOPY_XML "${CMAKE_CURRENT_SOURCE_DIR}/wlr-screencopy-unstable-v1.xml")
SET(SCREENCOPY_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/wlr-screencopy-unstable-v1-protocol.c")
SET(SCREENCOPY_HEADER "${CMAKE_CURRENT_BINARY_DIR}/wlr-screencopy-unstable-v1-client-protocol.h")

add_custom_command(
		OUTPUT "${SCREENCOPY_SOURCE}"
		COMMAND "${WAYLAND_SCANNER_EXEC}" "private-code" "<" "${SCREENCOPY_XML}" ">" "${SCREENCOPY_SOURCE}"
		DEPENDS "${SCREENCOPY_XML}"
		COMMENT "Generate wlr-screencopy protocol"
)

add_custom_command(
		OUTPUT "${SCREENCOPY_HEADER}"
		COMMAND "${WAYLAND_SCANNER_EXEC}" "client-header" "<" "${SCREENCOPY_XML}" ">" "${SCREENCOPY_HEADER}"
		DEPENDS "${SCREENCOPY_XML}"
		COMMENT "Generate wlr-screencopy header"
)

add_library(wlr_screencopy STATIC "${SCREENCOPY_SOURCE}" "${SCREENCOPY_HEADER}")
set_target_properties(wlr_screencopy PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Set all source files module uses
SET (SRC WlScreen.cpp
		 WlScreen.h)

add_library(${MODULE} MODULE ${SRC})
target_include_directories(${MODULE} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(${MODULE} wlr_screencopy ${LIBNAME} ${WAYLAND_LIBRARIES})

YURI_INSTALL_MODULE(${MODULE})
