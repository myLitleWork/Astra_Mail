cmake_minimum_required(VERSION 3.7)
project(AstraMail)

# ---------------- C++ ----------------
add_compile_options(-std=gnu++17)


# ---------------- OPTIONS ----------------
option(WITH_SODIUM "Enable libsodium backend" ON)

# ---------------- JSON ----------------


# ---------------- SOURCES ----------------
file(GLOB_RECURSE ASTRA_CPP
        ${CMAKE_SOURCE_DIR}/src/*.cpp
        )

set(SQLITE_C
        ${CMAKE_SOURCE_DIR}/src/SQLite/sqlite3.c
        )

set_source_files_properties(${SQLITE_C}
        PROPERTIES
        LANGUAGE C
        COMPILE_DEFINITIONS SQLITE_OS_UNIX
        )

set(SECRET_SOURCES
        secrets/SecretManager.cpp
        )

# ---------------- LIBSODIUM ----------------
if (WITH_SODIUM AND UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSODIUM REQUIRED libsodium)

    list(APPEND SECRET_SOURCES
            secrets/SecretBackendSodium.cpp
            secrets/SecretBackendSelector_sodium.cpp
            )
else()
    list(APPEND SECRET_SOURCES
            secrets/SecretBackendStub.cpp
            secrets/SecretBackendSelector_stub.cpp
            )
endif()

# ---------------- EXECUTABLE ----------------
add_executable(AstraMail
        main.cpp
        ${ASTRA_CPP}
        ${SQLITE_C}
        ${SECRET_SOURCES}
        )

target_link_libraries(AstraMail PRIVATE stdc++fs)


# ---------------- INCLUDES ----------------
target_include_directories(AstraMail PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/secrets
        ${CMAKE_SOURCE_DIR}/src/SQLite
        ${LIBSODIUM_INCLUDE_DIRS}
        )

# ---------------- FLAGS ----------------
target_compile_options(AstraMail PRIVATE
        -no-pie
        )

set_target_properties(AstraMail PROPERTIES
        LINK_FLAGS "-no-pie -static-libgcc -static-libstdc++"
        )


# ---------------- LIBS ----------------
target_link_libraries(AstraMail PRIVATE

        stdc++fs
        pthread
        dl
        ${LIBSODIUM_LIBRARIES}
        )

# ---------------- INSTALL ----------------
install(TARGETS AstraMail
        RUNTIME DESTINATION /usr/local/bin
        )

# ---------------- EXPORT ----------------
set(ASTRA_EXPORT_DIR "$ENV{HOME}/export/AstraMail")

add_custom_command(
        TARGET AstraMail
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ASTRA_EXPORT_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:AstraMail>
        ${ASTRA_EXPORT_DIR}/AstraMail
)
