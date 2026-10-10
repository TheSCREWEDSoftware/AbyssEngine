set(BUILD_SHARED_LIBS OFF)
set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_C_STANDARD 99)
set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
#enable_testing()
#add_compile_options(-fsanitize=address -g)
#add_link_options(-fsanitize=address)


if (NOT CMAKE_BUILD_TYPE)
    message(STATUS "No build type selected, defaulting to Release")
    set(CMAKE_BUILD_TYPE Release)
endif ()

#turn on all warnings
if (CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
endif ()

if (CMAKE_C_COMPILER_ID MATCHES "MSVC")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4")
endif ()

if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-gnu-zero-variadic-macro-arguments")
endif ()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    # Homebrew ships libarchive keg only, include dirs have to be set manually
    execute_process(
            COMMAND brew --prefix libarchive
            OUTPUT_VARIABLE LIBARCHIVE_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY
    )
    set(LibArchive_INCLUDE_DIR "${LIBARCHIVE_PREFIX}/include")
endif ()

# --- Debug features ---------------------------------------------------------------------------------------------------
# Shown together as the "DEBUG" group in cmake-gui (tick "Grouped"). The sub-options only appear while DEBUG_FEATURES is on.
include(CMakeDependentOption)
option(DEBUG_FEATURES "Enable developer/debug features" ON)
cmake_dependent_option(DEBUG_SKIP_INTRO "Skip the intro videos and start at the main menu" ON "DEBUG_FEATURES" OFF)
cmake_dependent_option(DEBUG_SHOW_CATALOG "Show the Catalog (item, class, sprite and bestiary browser) on the main menu" ON
                       "DEBUG_FEATURES" OFF)

option(WITHOUT_GIT "Don't ask git for the commit shown on the main menu" OFF)
set(ABYSS_VERSION_TEXT "" CACHE STRING "Override the commit hash shown on the main menu (CI passes the GitHub sha)")
