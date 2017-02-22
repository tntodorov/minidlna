
if (NOT WIN32)
    find_package (PkgConfig)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules (LIBEXIF QUIET libexif)
    endif (PKG_CONFIG_FOUND)
endif (NOT WIN32)

find_path (LIBEXIF_INCLUDE_DIR libexif/exif-tag.h PATHS ${LIBEXIF_INCLUDE_DIRS} ${WIN32_LIBAV_DIR}/include)
list (APPEND LIBEXIF_INCLUDE_DIRS ${LIBEXIF_INCLUDE_DIR})
find_library (LIBEXIF_LIBRARY NAMES exif HINTS ${LIBEXIF_LIBDIR} ${LIBEXIF_LIBRARY_DIRS} ${WIN32_LIBAV_DIR}/bin)
list (APPEND LIBEXIF_LIBRARIES ${LIBEXIF_LIBRARY})

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (LibExif DEFAULT_MSG LIBEXIF_LIBRARIES LIBEXIF_INCLUDE_DIRS)
set (LIBEXIF_FOUND ${LibExif_FOUND})

if (LibExif_FOUND)
    set (LIBEXIF_LIBRARIES ${LIBEXIF_LIBRARY})
endif (LibExif_FOUND)

mark_as_advanced (LIBEXIF_INCLUDE_DIRS LIBEXIF_LIBRARIES)