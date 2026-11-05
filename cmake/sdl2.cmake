include(FetchContent)

option(ECHO_SDL2_BUNDLED "Bundled SDL2 instead of system" ON)

if(NOT ECHO_SDL2_BUNDLED)
    find_package(SDL2 CONFIG QUIET)
    if(SDL2_FOUND)
        message(STATUS "Using system SDL2")
    else()
        set(ECHO_SDL2_BUNDLED ON)
    endif()
endif()

if(ECHO_SDL2_BUNDLED)
    message(STATUS "Using bundled SDL2")

    FetchContent_Declare(
        SDL2
        URL "https://github.com/libsdl-org/SDL/releases/download/release-2.30.10/SDL2-2.30.10.tar.gz"
        URL_HASH SHA256=f59adf36a0fcf4c94198e7d3d776c1b3824211ab7aeebeb31fe19836661196aa
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )

    FetchContent_GetProperties(SDL2)
    if(NOT SDL2_POPULATED)
        set(FETCHCONTENT_BASE_DIR "${PROJECT_SOURCE_DIR}/.cache/deps")
        message(STATUS "SDL2 FetchContent base: ${FETCHCONTENT_BASE_DIR}")
    endif()

    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(SDL2)
endif()
message(STATUS "SDL2: ${SDL2_LIBRARY}")