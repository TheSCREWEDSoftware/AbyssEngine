cmake_minimum_required(VERSION 3.20)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

project(AbyssEngine)
enable_testing()

include(Package)
include(BuildOptions)
include(Dependencies)
include(SetupSourceGroups)

add_executable(${PROJECT_NAME} WIN32)
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE src/AbyssEngine.c)
add_subdirectory(src)
setup_source_groups(src)

if (APPLE)
    find_library(OSX_VIDEOTOOLBOX VideoToolbox)
    find_library(OSX_COREMEDIA CoreMedia)
    find_library(OSX_SECURITY Security)
    set(ADDITIONAL_LIBRARIES
            ${OSX_VIDEOTOOLBOX}
            ${OSX_COREMEDIA}
            ${OSX_SECURITY}
    )
elseif (UNIX)
    set(ADDITIONAL_LIBRARIES "m")
endif ()

target_link_libraries(AbyssEngine PRIVATE ${ABYSS_DEPENDENCIES} ${ADDITIONAL_LIBRARIES})

# Git commit shown on the main menu: revision.h is written now and refreshed on every build
include(genrev)
add_custom_target(revision_h ALL
        COMMAND "${CMAKE_COMMAND}" -DBUILDDIR=${CMAKE_BINARY_DIR} -DSOURCEDIR=${CMAKE_SOURCE_DIR}
        "-DABYSS_VERSION_TEXT=${ABYSS_VERSION_TEXT}" "-DWITHOUT_GIT=${WITHOUT_GIT}" "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}"
        -P "${CMAKE_SOURCE_DIR}/cmake/genrev.cmake"
        BYPRODUCTS "${CMAKE_BINARY_DIR}/revision.h"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Checking git revision")
add_dependencies(${PROJECT_NAME} revision_h)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR})
target_compile_definitions(${PROJECT_NAME} PRIVATE "ABYSS_BUILD_TYPE=\"$<IF:$<CONFIG:>,Unknown,$<CONFIG>>\"")

if (DEBUG_SKIP_INTRO)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ABYSS_DEBUG_SKIP_INTRO)
endif ()
if (DEBUG_SHOW_CATALOG)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ABYSS_DEBUG_SHOW_CATALOG)
endif ()

include(AutoClangFormat)

if (APPLE)
    if (NOT DEFINED GITHUB_ACTIONS)
        # Add the clang-format target as a dependency for Xcode
        add_dependencies(${PROJECT_NAME} clang-format)
    endif ()

    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/extra/macos/" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/AbyssEngine.app/Contents/Resources")
    set_target_properties(AbyssEngine PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "Abyss Engine"
            MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
            PRODUCT_BUNDLE_IDENTIFIER "com.abyssengine"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.abyssengine"
            MACOSX_BUNDLE_ICON_FILE "icon.icns"
            #MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/extra/macos/MacOSXBundleInfo.plist.in"
            MACOSX_BUNDLE_COPYRIGHT "(c) 2023 Timothy Sarbin"
    )

    set_source_files_properties("extra/macos/icon.icns" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
    )

    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install path prefix, prepended onto install directories" FORCE)
else ()
    # Ship the config template next to the executable as abyss.ini.dist.
    # Copy it to abyss.ini (same folder) and edit it; the engine reads that file first.
    add_custom_command(TARGET AbyssEngine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/content/abyss.ini $<TARGET_FILE_DIR:AbyssEngine>/abyss.ini.dist)
endif ()


package_abyss_engine()
add_subdirectory(tests)
