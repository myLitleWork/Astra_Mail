set(CONFIGS_CPP
        ${CMAKE_SOURCE_DIR}/src/Configs/Confg/ConfigBootstrap.cpp
        ${CMAKE_SOURCE_DIR}/src/Configs/Confg/ConfigDefaults.cpp
        ${CMAKE_SOURCE_DIR}/src/Configs/Confg/ConfigPaths.cpp
        ${CMAKE_SOURCE_DIR}/src/Configs/Editor/EditorLauncher.cpp
        )

add_library(AstraMail_configs STATIC
        ${CONFIGS_CPP}
        )

target_include_directories(AstraMail_configs PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        )

# Uses fsu::getExecutableDir() from UtilsFS.
target_link_libraries(AstraMail_configs PRIVATE
        AstraMail_utilsfs
        )

