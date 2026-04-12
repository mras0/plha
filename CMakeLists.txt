cmake_minimum_required(VERSION 3.14)
project(plha CXX)

if (MSVC)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Supported build configurations" FORCE)

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest /Zc:preprocessor /permissive- /Zi /Zo /sdl /W4")
    #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4201") # C4201: nonstandard extension used: nameless struct/union
    if (NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /JMC")
    endif()
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG")
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
    add_definitions("-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_WARNINGS")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++23 -Wall -Wextra -pedantic -g")
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-gnu-anonymous-struct -Wno-nested-anon-types -Wno-unused-const-variable")
    endif()
endif()

if (WIN32)
    add_definitions("-DWIN32 -D_WIN32 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x600")
endif()

add_library(plha_lib STATIC
    crc16.cpp crc16.h
    fileio.cpp fileio.h
    util.cpp util.h
    lhaheader.cpp lhaheader.h
    lhafile.cpp lhafile.h
    lhaconsts.cpp lhaconsts.h
    lhacompress.cpp lhacompress.h
    ibs.cpp ibs.h
    decompress.cpp decompress.h
    hufftable.cpp hufftable.h
    dynhuff.cpp dynhuff.h
    obs.cpp obs.h
    lz.cpp lz.h
    huffcoder.cpp huffcoder.h
    compress.cpp compress.h
    )

add_executable(test_decompress
    test_decompress.cpp
    )
target_link_libraries(test_decompress plha_lib)

add_executable(test_compress
    test_compress.cpp
    )
target_link_libraries(test_compress plha_lib)

add_executable(test_archive
    test_archive.cpp
    )
target_link_libraries(test_archive plha_lib)

add_executable(plha
    main.cpp
    )
target_link_libraries(plha plha_lib)

