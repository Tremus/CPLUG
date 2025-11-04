# This file is for building the example project ONLY. It's not meant to be a module included as a subdirectory.
# If you're interested using CMake to build your plugins, read the code here, then copy/paste and modify it in
# your own projects. It's easy!
cmake_minimum_required(VERSION 3.14)
project(cplug_example
    VERSION 1.0.0
    DESCRIPTION "Example plugin using CPLUG"
    )

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 98)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})
# Windows paths are complicated
string(REPLACE "/" "\\\\" SOURCE_DIR_WIN ${PROJECT_SOURCE_DIR})
string(REPLACE "/" "\\\\" BINARY_DIR_WIN ${CMAKE_BINARY_DIR})

if (APPLE)
    enable_language(OBJC)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_STANDARD 20) # required for c++ clap build
endif()

if (CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_options(/W3 /FI ${CMAKE_CURRENT_SOURCE_DIR}/example/config.h)
    set(WINDOWS_EXPORT_ALL_SYMBOLS FALSE)
else()
    add_compile_options(-include${CMAKE_CURRENT_SOURCE_DIR}/example/config.h)
endif()
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    add_compile_options(
        -Werror=return-type
        -Werror=shadow
        -Wunused-function

        -Wno-deprecated
        -Wno-multichar
        -Wno-nullability-completeness
        -Wno-writable-strings
    )
endif()

if (WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# settings used across all formats
include_directories(src)

set(PRODUCT_NAME "CPLUG Example")
set(PRODUCT_COPYRIGHT CPLUG)
set(PLUGIN_BUNDLE_ID "com.cplug.example.plugin")
set(APP_BUNDLE_ID "com.cplug.example.app")
# Full list of AU types & tags here:
# https://developer.apple.com/library/archive/documentation/General/Conceptual/ExtensibilityPG/AudioUnit.html
# The properties:
set(MACOSX_BUNDLE_TAGS "<string>Synthesizer</string>") # Predefined tags are: Bass, Delay, Distortion, Drums, Dynamics, Dynamics Processor, Effects, Equalizer, Filter, Format Converter, Guitar, Imaging, MIDI, Mixer, Offline Effect, Output, Panner, Pitch, Reverb, Sampler, Synthesizer, Time Effect, Vocal. But you can use others.
set(MACOSX_BUNDLE_TYPE "aumu") # "aufx" for Effect, "augn" for Generator, "aumu" for Instrument, "aufm" for Music Effect.
set(MACOSX_BUNDLE_SUBTYPE "Xmpl") # For AU and GarageBand 10.3 compatibility, the first letter must be upper-case, the others lower-case.
set(MACOSX_BUNDLE_MANUFACTURER "Cplg") # For AU and GarageBand 10.3 compatibility, the first letter must be upper-case, the others lower-case.
# Convert version string (1.0.0) > integer (65536)
math(EXPR MACOSX_BUNDLE_VERSION_INT "${PROJECT_VERSION_MAJOR} << 16 | ${PROJECT_VERSION_MINOR} << 8 | ${PROJECT_VERSION_PATCH}" OUTPUT_FORMAT DECIMAL) # cool trick bro

# .plists are generated using a template
# The params @bundle_identifier@ and @bundle_type@ are used by the .plist
function(configure_info_plist target_name bundle_identifier bundle_type bundle_extension)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/example/Info.plist.in
        ${CMAKE_BINARY_DIR}/${bundle_type}.plist
        @ONLY
    )

    set_target_properties(${target_name} PROPERTIES
        BUNDLE True
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME ${PROJECT_NAME}
        BUNDLE_EXTENSION ${bundle_extension}
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/${bundle_type}.plist
        CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.${bundle_extension}
    )
endfunction()


# ██████╗ ██╗     ██╗   ██╗ ██████╗ ██╗███╗   ██╗
# ██╔══██╗██║     ██║   ██║██╔════╝ ██║████╗  ██║
# ██████╔╝██║     ██║   ██║██║  ███╗██║██╔██╗ ██║
# ██╔═══╝ ██║     ██║   ██║██║   ██║██║██║╚██╗██║
# ██║     ███████╗╚██████╔╝╚██████╔╝██║██║ ╚████║
# ╚═╝     ╚══════╝ ╚═════╝  ╚═════╝ ╚═╝╚═╝  ╚═══╝

# All Plugin formats in one
if (APPLE)
    add_library(${PROJECT_NAME}_plugin MODULE
        example/example.m
        src/cplug_auv2.c
        src/cplug_clap.c
        src/cplug_vst3.c
        )
    target_link_libraries(${PROJECT_NAME}_plugin PRIVATE "-framework AudioToolbox -framework Cocoa") # -framework AudioToolbox not actually required...

    configure_info_plist(${PROJECT_NAME}_plugin ${PLUGIN_BUNDLE_ID} "BNDL" "clap")

    file(TOUCH_NOCREATE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap/Contents/PkgInfo")
    file(WRITE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap/Contents/PkgInfo" "BNDL????")

    target_compile_definitions(${PROJECT_NAME}_plugin PRIVATE
        CPLUG_AUV2_VERSION_INT=${MACOSX_BUNDLE_VERSION_INT}
        CPLUG_AUV2_BUNDLE_ID="${PLUGIN_BUNDLE_ID}"
        CPLUG_BUILD_AUV2=1
    )

    add_custom_command(TARGET ${PROJECT_NAME}_plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.vst3"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.component"

        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap" "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap" "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.component"

        COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap to ~/Library/Audio/Plug-Ins/CLAP/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap" "~/Library/Audio/Plug-Ins/CLAP/${PROJECT_NAME}.clap"

        COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.vst3 to ~/Library/Audio/Plug-Ins/VST3/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.vst3" "~/Library/Audio/Plug-Ins/VST3/${PROJECT_NAME}.vst3"

        COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.component to ~/Library/Audio/Plug-Ins/Components/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.component" "~/Library/Audio/Plug-Ins/Components/${PROJECT_NAME}.component"
    )
elseif(WIN32)
    add_library(${PROJECT_NAME}_plugin MODULE
        example/example.c
        src/cplug_clap.c
        src/cplug_vst3.c
        )
    set_target_properties(${PROJECT_NAME}_plugin PROPERTIES
        OUTPUT_NAME ${PROJECT_NAME}_plugin
        PDB_NAME ${PROJECT_NAME}_plugin
        )

    set(VST3_BUILD_PATH "${BINARY_DIR_WIN}\\${CMAKE_BUILD_TYPE}\\${PROJECT_NAME}.vst3")

    # All this post build stuff fails with MSVC
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "MSVC" AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        add_custom_command(TARGET ${PROJECT_NAME}_plugin POST_BUILD
            # https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Locations+Format/Plugin+Format.html
            COMMAND echo Building VST3 folder: ${VST3_BUILD_PATH}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${VST3_BUILD_PATH}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_BUILD_PATH}\\Contents"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_BUILD_PATH}\\Contents\\x86_64-win"
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}\\${CMAKE_BUILD_TYPE}\\${PROJECT_NAME}_plugin.dll" "${VST3_BUILD_PATH}\\Contents\\x86_64-win\\${PROJECT_NAME}.vst3"
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}\\example\\PlugIn.ico"                     "${VST3_BUILD_PATH}\\PlugIn.ico"
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}\\example\\desktop.ini.in"                 "${VST3_BUILD_PATH}\\desktop.ini"
            COMMAND attrib +s +h ${VST3_BUILD_PATH}\\desktop.ini
            COMMAND attrib +s +h ${VST3_BUILD_PATH}\\PlugIn.ico
            COMMAND attrib +s ${VST3_BUILD_PATH}

            # The advantage of xcopy over cmake -E copy is that xcopy will let you copy the folder attributes we set above
            COMMAND echo Installing ${VST3_BUILD_PATH} to %LOCALAPPDATA%\\Programs\\Common\\VST3
            COMMAND xcopy ${VST3_BUILD_PATH} %LOCALAPPDATA%\\Programs\\Common\\VST3\\${PROJECT_NAME}.vst3\\ /s /e /h /y /q

            COMMAND echo Installing ${CMAKE_BINARY_DIR}\\${CMAKE_BUILD_TYPE}\\${PROJECT_NAME}_plugin.dll to %LOCALAPPDATA%\\Programs\\Common\\CLAP
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}\\${CMAKE_BUILD_TYPE}\\${PROJECT_NAME}_plugin.dll" "%LOCALAPPDATA%\\Programs\\Common\\CLAP\\${PROJECT_NAME}.clap"
            )
    endif() # MSVC
endif()


# ███████╗████████╗ █████╗ ███╗   ██╗██████╗  █████╗ ██╗      ██████╗ ███╗   ██╗███████╗
# ██╔════╝╚══██╔══╝██╔══██╗████╗  ██║██╔══██╗██╔══██╗██║     ██╔═══██╗████╗  ██║██╔════╝
# ███████╗   ██║   ███████║██╔██╗ ██║██║  ██║███████║██║     ██║   ██║██╔██╗ ██║█████╗  
# ╚════██║   ██║   ██╔══██║██║╚██╗██║██║  ██║██╔══██║██║     ██║   ██║██║╚██╗██║██╔══╝  
# ███████║   ██║   ██║  ██║██║ ╚████║██████╔╝██║  ██║███████╗╚██████╔╝██║ ╚████║███████╗
# ╚══════╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═══╝╚═════╝ ╚═╝  ╚═╝╚══════╝ ╚═════╝ ╚═╝  ╚═══╝╚══════╝

if (WIN32)
    add_executable(${PROJECT_NAME}_app WIN32 src/cplug_standalone_win.c example/example.c)
elseif (APPLE)
    add_executable(${PROJECT_NAME}_app MACOSX_BUNDLE src/cplug_standalone_osx.m example/example.m)
    target_link_libraries(${PROJECT_NAME}_app PRIVATE "-framework Cocoa -framework CoreMIDI -framework CoreAudio -framework CoreServices")
    configure_info_plist(${PROJECT_NAME}_app ${APP_BUNDLE_ID} "APPL" "app")

    file(TOUCH_NOCREATE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.app/Contents/PkgInfo")
    file(WRITE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.app/Contents/PkgInfo" "APPL????")
endif()


# ██╗  ██╗ ██████╗ ████████╗██████╗ ███████╗██╗      ██████╗  █████╗ ██████╗
# ██║  ██║██╔═══██╗╚══██╔══╝██╔══██╗██╔════╝██║     ██╔═══██╗██╔══██╗██╔══██╗
# ███████║██║   ██║   ██║   ██████╔╝█████╗  ██║     ██║   ██║███████║██║  ██║
# ██╔══██║██║   ██║   ██║   ██╔══██╗██╔══╝  ██║     ██║   ██║██╔══██║██║  ██║
# ██║  ██║╚██████╔╝   ██║   ██║  ██║███████╗███████╗╚██████╔╝██║  ██║██████╔╝
# ╚═╝  ╚═╝ ╚═════╝    ╚═╝   ╚═╝  ╚═╝╚══════╝╚══════╝ ╚═════╝ ╚═╝  ╚═╝╚═════╝

set(HOTRELOAD_LIB_NAME ${PROJECT_NAME}_hotreload_lib)

if (WIN32 AND CMAKE_BUILD_TYPE MATCHES Debug)
    add_library(${HOTRELOAD_LIB_NAME} MODULE example/example.c)
    target_compile_definitions(${HOTRELOAD_LIB_NAME} PRIVATE CPLUG_SHARED)

    add_executable(${PROJECT_NAME}_hotreload WIN32 src/cplug_standalone_win.c)
    target_compile_definitions(${PROJECT_NAME}_hotreload PRIVATE
        HOTRELOAD_WATCH_DIR="${SOURCE_DIR_WIN}\\\\example"
        HOTRELOAD_LIB_PATH="${BINARY_DIR_WIN}\\\\${CMAKE_BUILD_TYPE}\\\\${HOTRELOAD_LIB_NAME}.dll"
        HOTRELOAD_BUILD_COMMAND="cmake --build ${BINARY_DIR_WIN} --config Debug --target ${HOTRELOAD_LIB_NAME}"
        CPLUG_SHARED
        )
    # Forces plugin to be built before the host
    add_dependencies(${PROJECT_NAME}_hotreload ${HOTRELOAD_LIB_NAME})
elseif (APPLE AND CMAKE_BUILD_TYPE MATCHES Debug)
    add_library(${HOTRELOAD_LIB_NAME} MODULE example/example.m)
    target_link_libraries(${HOTRELOAD_LIB_NAME} PRIVATE "-framework Cocoa")

    add_executable(${PROJECT_NAME}_hotreload MACOSX_BUNDLE src/cplug_standalone_osx.m)

    file(TOUCH_NOCREATE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}_hotreload.app/Contents/PkgInfo")
    file(WRITE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}_hotreload.app/Contents/PkgInfo" "APPL????")

    target_link_libraries(${PROJECT_NAME}_hotreload PRIVATE "-framework Cocoa -framework CoreMIDI -framework CoreAudio -framework CoreServices")

    target_compile_definitions(${PROJECT_NAME}_hotreload PRIVATE
        HOTRELOAD_WATCH_DIR="${PROJECT_SOURCE_DIR}/example"
        HOTRELOAD_LIB_PATH="${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/lib${HOTRELOAD_LIB_NAME}.so"
        HOTRELOAD_BUILD_COMMAND="cmake --build ${CMAKE_BINARY_DIR} --config Debug --target ${HOTRELOAD_LIB_NAME}"
        CPLUG_SHARED
        )
    add_dependencies(${PROJECT_NAME}_hotreload ${HOTRELOAD_LIB_NAME})
endif()

# ████████╗███████╗███████╗████████╗
# ╚══██╔══╝██╔════╝██╔════╝╚══██╔══╝
#    ██║   █████╗  ███████╗   ██║   
#    ██║   ██╔══╝  ╚════██║   ██║   
#    ██║   ███████╗███████║   ██║   
#    ╚═╝   ╚══════╝╚══════╝   ╚═╝   

if (APPLE)
    add_executable(test_compile_objcpp test_compile.mm)
    target_compile_definitions(test_compile_objcpp PRIVATE)
    target_link_libraries(test_compile_objcpp PRIVATE "-framework Cocoa -framework CoreMIDI -framework CoreAudio -framework AudioToolbox")
    target_compile_definitions(test_compile_objcpp PRIVATE
        CPLUG_AUV2_VERSION_INT=${MACOSX_BUNDLE_VERSION_INT}
        CPLUG_AUV2_BUNDLE_ID="${PLUGIN_BUNDLE_ID}"
        CPLUG_BUILD_AUV2=1
    )
else()
    add_executable(test_compile_cpp WIN32 test_compile.cpp)
endif()
