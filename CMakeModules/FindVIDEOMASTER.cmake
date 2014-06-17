

SET(VIDEOMASTER_INC $ENV{VIDEOMASTER_INCLUDE})
SET(VIDEOMASTER_LIBDIR $ENV{VIDEOMASTER_LIBDIR})

find_path(VIDEOMASTER_VIDEOMASTER_INCLUDE_DIR VideoMasterHD_Core.h PATHS
  ${VIDEOMASTER_INC}
  /usr/local/include/videomaster
  /usr/local/include/videomaster/include
  /usr/include/videomaster
  /usr/include/videomaster/include
  /usr/include
  /usr/local/include    
 )

  
find_library(VIDEOMASTER_VIDEOMASTER_LIBRARY NAMES videomasterhd PATH 
	${VIDEOMASTER_LIBDIR}
 	/usr/lib/
 	/usr/local/lib)

find_library(VIDEOMASTER_VIDEOMASTER_LIBRARY_AUDIO NAMES videomasterhd_audio PATH 
	${VIDEOMASTER_LIBDIR}
 	/usr/lib/
 	/usr/local/lib) 
 
IF (VIDEOMASTER_VIDEOMASTER_LIBRARY AND VIDEOMASTER_VIDEOMASTER_INCLUDE_DIR)
	SET(VIDEOMASTER_INCLUDE_DIRS ${VIDEOMASTER_VIDEOMASTER_INCLUDE_DIR} )
	SET(VIDEOMASTER_INCLUDE_DIR ${VIDEOMASTER_VIDEOMASTER_INCLUDE_DIR} ) # for backward compatiblity
	SET(VIDEOMASTER_LIBRARY ${VIDEOMASTER_VIDEOMASTER_LIBRARY})
	SET(VIDEOMASTER_LIBRARIES ${VIDEOMASTER_VIDEOMASTER_LIBRARY})
	SET(VIDEOMASTER_AUDIO_LIBRARIES ${VIDEOMASTER_VIDEOMASTER_LIBRARY_AUDIO})
#	if (VIDEOMASTER_VIDEOMASTER_INCLUDE_DIR AND EXISTS "${VIDEOMASTER_VIDEOMASTER_INCLUDE_DIR}/DeckLinkAPIVersion.h")
#		file(STRINGS "${VIDEOMASTER_VIDEOMASTER_INCLUDE_DIR}/DeckLinkAPIVersion.h" decklink_version_str REGEX "^#define[ \t]+BLACKMAGIC_VIDEOMASTER_API_VERSION_STRING[ \t]+\".+\"")
#		string(REGEX REPLACE "^#define[ \t]+BLACKMAGIC_VIDEOMASTER_API_VERSION_STRING[ \t]+\"([^\"]+)\".*" "\\1" VIDEOMASTER_VERSION_STRING "${decklink_version_str}")
#		unset(decklink_version_str)
		#MESSAGE(${VIDEOMASTER_VERSION_STRING})
#	endif ()
ENDIF()      

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VIDEOMASTER  
                                  REQUIRED_VARS VIDEOMASTER_LIBRARY VIDEOMASTER_INCLUDE_DIR
                                  VERSION_VAR VIDEOMASTER_VERSION_STRING)


mark_as_advanced(VIDEOMASTER_VIDEOMASTER_INCLUDE_DIR VIDEOMASTER_VIDEOMASTER_LIBRARY )