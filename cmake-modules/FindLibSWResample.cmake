# - Find libswresample
# Find the FFMPEG audio resampling includes and library
# This module defines
#  LIBSWRESAMPLE_INCLUDE_DIRS, where to find swresample.h, etc.
#  LIBSWRESAMPLE_LIBRARIES, the libraries needed to use libswresample.
#  LIBSWRESAMPLE_FOUND, If false, do not try to use libswresample.
# also defined, but not for general use are
#  LIBSWRESAMPLE_LIBRARY, where to find the JPEG library.

#
# Copyright 2010-2013 Bluecherry
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of
# the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#

if (NOT WIN32)
    find_package (PkgConfig)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules (LIBSWRESAMPLE QUIET libswresample)
    endif (PKG_CONFIG_FOUND)
endif (NOT WIN32)

find_path (LIBSWRESAMPLE_INCLUDE_DIR libswresample/swresample.h ${LIBSWRESAMPLE_INCLUDE_DIRS} ${WIN32_LIBAV_DIR}/include)
list (APPEND LIBSWRESAMPLE_INCLUDE_DIRS ${LIBSWRESAMPLE_INCLUDE_DIR})
find_library (LIBSWRESAMPLE_LIBRARY NAMES swresample HINTS ${LIBSWRESAMPLE_LIBDIR} ${LIBSWRESAMPLE_LIBRARY_DIRS} ${WIN32_LIBAV_DIR}/bin)
list (APPEND LIBSWRESAMPLE_LIBRARIES ${LIBSWRESAMPLE_LIBRARY})

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (LibSWresample DEFAULT_MSG LIBSWRESAMPLE_LIBRARIES LIBSWRESAMPLE_INCLUDE_DIRS)
set (LIBSWRESAMPLE_FOUND ${LibSWResample_FOUND})

if (LIBSWRESAMPLE_FOUND)
    set (LIBSWRESAMPLE_LIBRARIES ${LIBSWRESAMPLE_LIBRARY})
endif (LIBSWRESAMPLE_FOUND)

mark_as_advanced (LIBSWRESAMPLE_INCLUDE_DIRS LIBSWRESAMPLE_LIBRARIES)