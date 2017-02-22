cmake_minimum_required(VERSION 3.6)
project(minidlna)

set(CMAKE_VERBOSE_MAKEFILE 1)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake-modules/")

set(SOURCE_FILES
        tagutils/tagutils.c
        tagutils/tagutils.h
        albumart.c
        albumart.h
        clients.c
        clients.h
        codelength.h
        config.h
        containers.c
        containers.h
        getifaddr.c
        getifaddr.h
#        icons.c
        image_utils.c
        image_utils.h
        inotify.c
        inotify.h
        libav.h
        log.c
        log.h
        metadata.c
        metadata.h
        minidlna.c
        minidlnapath.h
        minidlnatypes.h
        minissdp.c
        minissdp.h
        minixml.c
        minixml.h
        options.c
        options.h
        playlist.c
        playlist.h
        process.c
        process.h
        scanner.c
        scanner.h
        scanner_sqlite.h
        sendfile.h
        sql.c
        sql.h
        tivo_beacon.c
        tivo_beacon.h
        tivo_commands.c
        tivo_commands.h
        tivo_utils.c
        tivo_utils.h
        upnpdescgen.c
        upnpdescgen.h
        upnpevents.c
        upnpevents.h
        upnpglobalvars.c
        upnpglobalvars.h
        upnphttp.c
        upnphttp.h
        upnpreplyparse.c
        upnpreplyparse.h
        upnpsoap.c
        upnpsoap.h
        utils.c
        utils.h
        uuid.c
        uuid.h metadata_ext.h metadata_ext.c)

include_directories(.)
include_directories(tagutils)

add_definitions(-D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64)

add_executable(minidlna ${SOURCE_FILES})

include(libav.cmake)

find_package(Threads)
find_package(Vorbis)
find_package(Ogg)
find_package(SQLite3)
find_package(FLAC)
find_package(Exif)
find_package(JPEG)
find_package(ID3Tag)
find_package(CURL)
find_package(Jansson)

target_link_libraries(minidlna ${minidlna_client_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${LIBVORBIS_LIBRARIES}
        ${LIBOGG_LIBRARIES} ${SQLITE3_LIBRARIES} ${LIBFLAC_LIBRARIES} ${LIBEXIF_LIBRARIES} ${JPEG_LIBRARIES}
        ${LIBID3TAG_LIBRARIES} ${CURL_LIBRARIES} ${LIBJANSSON_LIBRARIES})