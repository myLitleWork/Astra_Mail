set(CORE_CPP
        ${CMAKE_SOURCE_DIR}/src/core/profile/ActiveProfileStorage.cpp
        ${CMAKE_SOURCE_DIR}/src/core/profile/LocalProfileCache.cpp
        ${CMAKE_SOURCE_DIR}/src/core/paths/AstraMailPaths.cpp

        ${CMAKE_SOURCE_DIR}/src/core/contacts/ContactsManager.cpp
        ${CMAKE_SOURCE_DIR}/src/core/contacts/ThunderbirdContactsImporter.cpp
        ${CMAKE_SOURCE_DIR}/src/core/contacts/CsvContactsImporter.cpp

        ${CMAKE_SOURCE_DIR}/src/core/thunderbird/ThunderbirdSettings.cpp

        ${CMAKE_SOURCE_DIR}/src/core/mail/MailFolderModels.cpp
        ${CMAKE_SOURCE_DIR}/src/core/mail/MailFolderReport.cpp
        ${CMAKE_SOURCE_DIR}/src/core/mail/MailFolderScanner.cpp
        ${CMAKE_SOURCE_DIR}/src/core/mail/MailSendPipeline.cpp
        ${CMAKE_SOURCE_DIR}/src/core/mail/MailDeliveryState.cpp
        ${CMAKE_SOURCE_DIR}/src/core/mail/MailNetworkSettings.cpp

        ${CMAKE_SOURCE_DIR}/src/core/mime/MimeFileBuilder.cpp
        ${CMAKE_SOURCE_DIR}/src/core/mime/MimeTemporaryFile.cpp

        ${CMAKE_SOURCE_DIR}/src/core/idle/IdleBackground.cpp
        ${CMAKE_SOURCE_DIR}/src/core/idle/IdleSettings.cpp
        )

add_library(AstraMail_core STATIC
        ${CORE_CPP}
        )

target_include_directories(AstraMail_core PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        )

# Needed because some cpp files include menu headers that include SecretManager.h.
target_include_directories(AstraMail_core PRIVATE
        ${CMAKE_SOURCE_DIR}/secrets
        )

target_link_libraries(AstraMail_core PRIVATE
        AstraMail_sqlite
        AstraMail_secrets
        )

