# Copied from http://code.google.com/p/osgaudio/source/browse/trunk/CMakeModules/FindFLAC.cmake
# Locate FLAC
# This module defines XXX_FOUND, XXX_INCLUDE_DIRS and XXX_LIBRARIES standard variables
#
# $FLACDIR is an environment variable that would
# correspond to the ./configure --prefix=$FLACDIR
# used in building FLAC.

if (NOT WIN32)
    find_package (PkgConfig)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules (LIBFLAC QUIET flac)
    endif (PKG_CONFIG_FOUND)
endif (NOT WIN32)

find_path (LIBFLAC_INCLUDE_DIR FLAC/all.h PATHS ${LIBFLAC_INCLUDE_DIRS} ${WIN32_LIBAV_DIR}/include)
list (APPEND LIBFLAC_INCLUDE_DIRS ${LIBFLAC_INCLUDE_DIR})
find_library (LIBFLAC_LIBRARY NAMES FLAC HINTS ${LIBFLAC_LIBDIR} ${LIBFLAC_LIBRARY_DIRS} ${WIN32_LIBAV_DIR}/bin)
list (APPEND LIBFLAC_LIBRARIES ${LIBFLAC_LIBRARY})

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (LibFlac DEFAULT_MSG LIBFLAC_LIBRARIES LIBFLAC_INCLUDE_DIRS)
set (LIBFLAC_FOUND ${LibFlac_FOUND})

if (LibFlac_FOUND)
    set (LIBFLAC_LIBRARIES ${LIBFLAC_LIBRARY})
endif (LibFlac_FOUND)

mark_as_advanced (LIBFLAC_INCLUDE_DIRS LIBFLAC_LIBRARIES)