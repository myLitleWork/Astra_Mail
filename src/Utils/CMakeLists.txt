set(UTILS_CPP
        ${CMAKE_SOURCE_DIR}/src/Utils/Base64.cpp
        ${CMAKE_SOURCE_DIR}/src/Utils/Confirm.cpp
        ${CMAKE_SOURCE_DIR}/src/Utils/EmlParser.cpp
        ${CMAKE_SOURCE_DIR}/src/Utils/FlashDriveScanner.cpp
        ${CMAKE_SOURCE_DIR}/src/Utils/ImapClient.cpp
        ${CMAKE_SOURCE_DIR}/src/Utils/ImapFolderFilter.cpp
        ${CMAKE_SOURCE_DIR}/src/Utils/ImapUtf7.cpp
        ${CMAKE_SOURCE_DIR}/src/Utils/SmtpClient.cpp
        ${CMAKE_SOURCE_DIR}/src/Utils/SubjectUtils.cpp
        ${CMAKE_SOURCE_DIR}/src/Utils/Utils.cpp
        )

add_library(AstraMail_utils STATIC
        ${UTILS_CPP}
        )

target_include_directories(AstraMail_utils PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        )

