set(UTILSFS_CPP
        ${CMAKE_SOURCE_DIR}/src/UtilsFS/FsUtils.cpp
        )

add_library(AstraMail_utilsfs STATIC
        ${UTILSFS_CPP}
        )

target_include_directories(AstraMail_utilsfs PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        )

