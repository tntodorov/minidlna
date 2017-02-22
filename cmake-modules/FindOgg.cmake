# Copied from http://code.google.com/p/osgaudio/source/browse/trunk/CMakeModules/FindOGG.cmake
# Locate OGG
# This module defines XXX_FOUND, XXX_INCLUDE_DIRS and XXX_LIBRARIES standard variables
#
# $OGGDIR is an environment variable that would
# correspond to the ./configure --prefix=$OGGDIR
# used in building OGG.

if (NOT WIN32)
    find_package (PkgConfig)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules (LIBOGG QUIET ogg)
    endif (PKG_CONFIG_FOUND)
endif (NOT WIN32)

find_path (LIBOGG_INCLUDE_DIR ogg/ogg.h PATHS ${LIBOGG_INCLUDE_DIRS} ${WIN32_LIBAV_DIR}/include)
list (APPEND LIBOGG_INCLUDE_DIRS ${LIBOGG_INCLUDE_DIR})
find_library (LIBOGG_LIBRARY NAMES ogg HINTS ${LIBOGG_LIBDIR} ${LIBOGG_LIBRARY_DIRS} ${WIN32_LIBAV_DIR}/bin)
list (APPEND LIBOGG_LIBRARIES ${LIBOGG_LIBRARY})

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (LibOgg DEFAULT_MSG LIBOGG_LIBRARIES LIBOGG_INCLUDE_DIRS)
set (LIBOGG_FOUND ${LibOgg_FOUND})

if (LibOgg_FOUND)
    set (LIBOGG_LIBRARIES ${LIBOGG_LIBRARY})
endif (LibOgg_FOUND)

mark_as_advanced (LIBOGG_INCLUDE_DIRS LIBOGG_LIBRARIES)