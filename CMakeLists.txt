cmake_minimum_required(VERSION 3.7)
project(AstraMail)
include(cmake/AstraMailVersion.cmake)

# ---------------- C++ ----------------


# ---------------- OPTIONS ----------------
option(WITH_SODIUM "Enable libsodium backend" ON)

# ---------------- JSON ----------------


# ---------------- MODULES ----------------
add_subdirectory(src/Utils)
add_subdirectory(src/UtilsFS)
add_subdirectory(src/Resolver)
add_subdirectory(src/menu)
add_subdirectory(src/Configs)
add_subdirectory(src/core)

# ---------------- LIBSODIUM ----------------
if (WITH_SODIUM AND UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSODIUM REQUIRED libsodium)
endif()

# ---------------- SQLITE (LIB) ----------------
set(SQLITE_C ${CMAKE_SOURCE_DIR}/src/SQLite/sqlite3.c)
set_source_files_properties(${SQLITE_C} PROPERTIES LANGUAGE C)
if (UNIX)
    set_source_files_properties(${SQLITE_C} PROPERTIES COMPILE_DEFINITIONS SQLITE_OS_UNIX)
endif()

add_library(AstraMail_sqlite STATIC
        ${SQLITE_C}
        src/SQLite/mySQLite.cpp
        src/SQLite/SQLiteConnectionManager.cpp
        )

target_include_directories(AstraMail_sqlite PUBLIC
        ${CMAKE_SOURCE_DIR}/src/SQLite
        )

# ---------------- SECRETS (LIB) ----------------
set(SECRET_SOURCES
        secrets/SecretManager.cpp
        )

if (WITH_SODIUM AND UNIX)
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

add_library(AstraMail_secrets STATIC
        ${SECRET_SOURCES}
        )

target_include_directories(AstraMail_secrets PUBLIC
        ${CMAKE_SOURCE_DIR}/secrets
        )

if (WITH_SODIUM AND UNIX)
    target_include_directories(AstraMail_secrets PRIVATE
            ${LIBSODIUM_INCLUDE_DIRS}
            )
    target_link_libraries(AstraMail_secrets PRIVATE
            ${LIBSODIUM_LIBRARIES}
            )
endif()

# ---------------- EXECUTABLE ----------------
add_executable(AstraMail
        main.cpp
        )

# ---------------- INCLUDES ----------------
target_include_directories(AstraMail PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        )

# ---------------- FLAGS ----------------
# Astra Linux 1.6 может не поддерживать C++17 в виде "language dialect" механизма CMake 3.7.
# Поэтому включаем стандарт явно через -std=..., а не через CMAKE_CXX_STANDARD/CXX_STANDARD property.
#
# Твой код использует:
# - std::experimental::* (это про библиотеку, обычно работает и без полного C++17 STL)
# - nested namespaces вида `namespace a::b` (это про язык и требует хотя бы режима C++17)
#
# Алгоритм:
# - пробуем -std=gnu++17
# - если не проходит, откатываемся на -std=gnu++14
include(CheckCXXCompilerFlag)
set(ASTRA_CXX_STD_FLAG "")
if (MSVC)
    # MSVC: /std:... (генераторный флаг; если ваш MSVC не поддерживает, то упадёт на компиляции)
    set(ASTRA_CXX_STD_FLAG "/std:c++17")
else()
    check_cxx_compiler_flag("-std=gnu++17" HAS_GNUXX17)
    if (HAS_GNUXX17)
        set(ASTRA_CXX_STD_FLAG "-std=gnu++17")
    else()
        check_cxx_compiler_flag("-std=gnu++14" HAS_GNUXX14)
        if (HAS_GNUXX14)
            set(ASTRA_CXX_STD_FLAG "-std=gnu++14")
        else()
            # Уж точно соберёмся на каком-то дефолте, но это редкий случай.
            set(ASTRA_CXX_STD_FLAG "")
        endif()
    endif()
endif()

foreach(_tgt AstraMail AstraMail_utils AstraMail_utilsfs AstraMail_resolver AstraMail_menu AstraMail_configs AstraMail_core AstraMail_sqlite AstraMail_secrets)
    if(TARGET ${_tgt} AND ASTRA_CXX_STD_FLAG)
        if (MSVC)
            target_compile_options(${_tgt} PRIVATE ${ASTRA_CXX_STD_FLAG})
        else()
            target_compile_options(${_tgt} PRIVATE
                    $<$<COMPILE_LANGUAGE:CXX>:${ASTRA_CXX_STD_FLAG}>
                    )
        endif()
    endif()
endforeach()

if (UNIX AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    target_compile_options(AstraMail PRIVATE
            -no-pie
            )

    set_target_properties(AstraMail PROPERTIES
            LINK_FLAGS "-no-pie -static-libgcc -static-libstdc++"
            )
endif()

# ---------------- LIBS ----------------
target_link_libraries(AstraMail PRIVATE

        AstraMail_utilsfs
        AstraMail_utils
        AstraMail_resolver
        AstraMail_menu
        AstraMail_configs
        AstraMail_core
        AstraMail_sqlite
        AstraMail_secrets
        )

if (UNIX)
    target_link_libraries(AstraMail PRIVATE
            stdc++fs
            pthread
            dl
            )
endif()

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
