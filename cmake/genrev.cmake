# Writes ${BUILDDIR}/revision_data.h with the git commit the engine was built from (modelled on AzerothCore's genrev).
# Runs at configure time and again on every build (the "revision_h" target), so the main menu always shows the
# commit that was actually compiled. Files are only rewritten when something changed, to avoid needless rebuilds.
#
#   ABYSS_VERSION_TEXT  overrides the hash (CI passes the GitHub sha)
#   WITHOUT_GIT         skip git entirely ("unknown")

if (NOT BUILDDIR)
    set(BUILDDIR ${CMAKE_BINARY_DIR})
endif ()
if (NOT SOURCEDIR)
    set(SOURCEDIR ${CMAKE_SOURCE_DIR})
endif ()

set(rev_hash "")
set(rev_date "")
set(rev_branch "")

if (NOT WITHOUT_GIT)
    if (NOT GIT_EXECUTABLE)
        find_program(GIT_EXECUTABLE git)
    endif ()
    if (NOT GIT_EXECUTABLE)
        set(GIT_EXECUTABLE "git")
    endif ()

    # "8e0201c3a1b2", or "8e0201c3a1b2+" when there are uncommitted changes
    execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --always --dirty=+ --abbrev=12 --match "NO_TAGS_PLEASE"
            WORKING_DIRECTORY "${SOURCEDIR}"
            OUTPUT_VARIABLE rev_hash
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    execute_process(
            COMMAND "${GIT_EXECUTABLE}" show -s --format=%ci
            WORKING_DIRECTORY "${SOURCEDIR}"
            OUTPUT_VARIABLE rev_date
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
            WORKING_DIRECTORY "${SOURCEDIR}"
            OUTPUT_VARIABLE rev_branch
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
endif ()

if (ABYSS_VERSION_TEXT)
    string(SUBSTRING "${ABYSS_VERSION_TEXT}" 0 12 rev_hash)
endif ()

if (NOT rev_hash)
    # No git (source zip, git not installed): still build, just without a commit to show
    set(rev_hash "unknown")
    set(rev_branch "local build")
    string(TIMESTAMP rev_date "%Y-%m-%d %H:%M:%S +0000" UTC)
endif ()
if (NOT rev_branch OR rev_branch STREQUAL "HEAD")
    set(rev_branch "detached")
endif ()

# How the libraries are linked: vcpkg "-static" triplets link everything into the exe
if (VCPKG_TARGET_TRIPLET MATCHES "static")
    set(rev_linkage "Static")
else ()
    set(rev_linkage "Dynamic")
endif ()

configure_file("${SOURCEDIR}/src/common/revision_data.h.in" "${BUILDDIR}/revision_data.h.tmp" @ONLY)
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${BUILDDIR}/revision_data.h.tmp" "${BUILDDIR}/revision_data.h")
file(REMOVE "${BUILDDIR}/revision_data.h.tmp")
