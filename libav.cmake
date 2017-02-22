find_package (LibAvCodec 56.60.100 REQUIRED)
find_package (LibAVFormat 56.40.101 REQUIRED)
find_package (LibAVUtil 54.31.100 REQUIRED)
find_package (LibSWResample 1.2.101 REQUIRED)

include_directories (${LIBAVCODEC_INCLUDE_DIRS})
include_directories (${LIBAVFORMAT_INCLUDE_DIRS})
include_directories (${LIBAVUTIL_INCLUDE_DIRS})
include_directories (${LIBSWRESAMPLE_INCLUDE_DIRS})

link_directories (${LIBAVCODEC_LIBRARY_DIRS})
link_directories (${LIBAVFORMAT_LIBRARY_DIRS})
link_directories (${LIBAVUTIL_LIBRARY_DIRS})
link_directories (${LIBSWRESAMPLE_LIBRARY_DIRS})

# __STDC_CONSTANT_MACROS is necessary for libav on Linux
add_definitions (-D__STDC_CONSTANT_MACROS)

list (APPEND minidlna_client_LIBRARIES
        ${LIBAVCODEC_LIBRARIES}
        ${LIBAVFORMAT_LIBRARIES}
        ${LIBAVUTIL_LIBRARIES}
        ${LIBSWRESAMPLE_LIBRARIES}
        )

get_filename_component (LIBAVCODEC_RPATH ${LIBAVCODEC_LIBRARY} PATH)
get_filename_component (LIBAVFORMAT_RPATH ${LIBAVFORMAT_LIBRARY} PATH)
get_filename_component (LIBAVUTIL_RPATH ${LIBAVUTIL_LIBRARY} PATH)
get_filename_component (LIBSWRESAMPLE_RPATH ${LIBSWRESAMPLE_LIBRARY} PATH)

if (LINUX)
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,${LIBAVCODEC_RPATH}")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,${LIBAVCODEC_LIBAVFORMAT}")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,${LIBAVUTIL_RPATH}")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,${LIBSWRESAMPLE_RPATH}")
endif (LINUX)