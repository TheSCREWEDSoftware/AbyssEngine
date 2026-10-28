set(BUILD_SHARED_LIBS OFF)
set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)
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
    # C4996: MSVC wants its own Windows-only versions of standard C functions (strcpy_s, fopen_s, ...). The engine
    # sticks to standard C so it builds everywhere; don't flag every strdup, sscanf and getenv.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
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
cmake_dependent_option(DEBUG_SHOW_MAP_EDITOR "Show the Map Editor and Testing Grounds on the main menu" ON
                       "DEBUG_FEATURES" OFF)
cmake_dependent_option(DEBUG_SHOW_OPTIONS_DEBUG "Show the Debug tab in Options (Map Editor auto-save, collision view, ...)"
                       ON "DEBUG_FEATURES" OFF)

# --- Defaults ---------------------------------------------------------------------------------------------------------
# Original: like Diablo II (click to walk, worn gear comes off when picked up, the game's control panel); Modern: keys to
# walk, gear stays worn until put down, the Modern control panel. Players can change each one in Options.
set(DEFAULT_STYLE "Original" CACHE STRING "Default style of the controls and settings: Original (like Diablo II) or Modern")
set_property(CACHE DEFAULT_STYLE PROPERTY STRINGS Original Modern)
if (NOT DEFAULT_STYLE MATCHES "^(Original|Classic|Modern)$") # (Classic: the old name for Original)
    message(FATAL_ERROR "DEFAULT_STYLE must be Original or Modern (it's \"${DEFAULT_STYLE}\")")
endif ()

option(WITHOUT_GIT "Don't ask git for the commit shown on the main menu" OFF)
set(ABYSS_VERSION_TEXT "" CACHE STRING "Override the commit hash shown on the main menu (CI passes the GitHub sha)")
