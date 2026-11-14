# Set name of the module
SET (MODULE rawavfile)

# Set all source files module uses
SET (SRC avcommon.h
		 avcommon.cpp
		 RawAVFile.cpp
		 RawAVFile.h
		 AVDecoder.h
		 AVDecoder.cpp
		 h264_helper.cpp
		 h264_helper.h
		 RawAVFilePlaylist.cpp
		 RawAVFilePlaylist.h
		 register.cpp )


# The code needs the channel layout API of FFmpeg 5.1+ (av_frame_get_channels, checked here
# before, is gone in FFmpeg 8 and disabled audio of file playback)

include_directories(${libavcodec_INCLUDE_DIRS})
link_directories(${libavcodec_LIBRARY_DIRS})
# You shouldn't need to edit anything below this line 
add_library(${MODULE} MODULE ${SRC})
target_link_libraries(${MODULE} ${LIBNAME}  ${LIBNAME_HELPER_LIBAV} ${libavformat_LIBRARIES})

YURI_INSTALL_MODULE(${MODULE})
