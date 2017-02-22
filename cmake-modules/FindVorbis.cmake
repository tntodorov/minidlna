# Copied from http://code.google.com/p/osgaudio/source/browse/trunk/CMakeModules/FindVORBIS.cmake
# Locate VORBIS
# This module defines XXX_FOUND, XXX_INCLUDE_DIRS and XXX_LIBRARIES standard variables
#
# $VORBISDIR is an environment variable that would
# correspond to the ./configure --prefix=$VORBISDIR
# used in building VORBIS.

if (NOT WIN32)
    find_package (PkgConfig)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules (LIBVORBIS QUIET vorbis)
    endif (PKG_CONFIG_FOUND)
endif (NOT WIN32)

find_path (LIBVORBIS_INCLUDE_DIR vorbis/vorbisfile.h PATHS ${LIBVORBIS_INCLUDE_DIRS} ${WIN32_LIBAV_DIR}/include)
list (APPEND LIBVORBIS_INCLUDE_DIRS ${LIBVORBIS_INCLUDE_DIR})
find_library (LIBVORBIS_LIBRARY NAMES vorbis HINTS ${LIBVORBIS_LIBDIR} ${LIBVORBIS_LIBRARY_DIRS} ${WIN32_LIBAV_DIR}/bin)
list (APPEND LIBVORBIS_LIBRARIES ${LIBVORBIS_LIBRARY})

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (LibVorbis DEFAULT_MSG LIBVORBIS_LIBRARIES LIBVORBIS_INCLUDE_DIRS)
set (LIBVORBIS_FOUND ${LibVorbis_FOUND})

if (LibVorbis_FOUND)
    set (LIBVORBIS_LIBRARIES ${LIBVORBIS_LIBRARY})
endif (LibVorbis_FOUND)

mark_as_advanced (LIBVORBIS_INCLUDE_DIRS LIBVORBIS_LIBRARIES)