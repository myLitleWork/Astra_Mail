set(RESOLVER_CPP
        ${CMAKE_SOURCE_DIR}/src/Resolver/Resolvers/AttachmentGroupsResolver.cpp
        ${CMAKE_SOURCE_DIR}/src/Resolver/Resolvers/ContactsRecipientResolver.cpp
        ${CMAKE_SOURCE_DIR}/src/Resolver/Resolvers/OutgoingSubjectResolver.cpp
        ${CMAKE_SOURCE_DIR}/src/Resolver/Resolvers/StubRecipientResolver.cpp
        ${CMAKE_SOURCE_DIR}/src/Resolver/Resolvers/SubjectFolderResolver.cpp
        )

add_library(AstraMail_resolver STATIC
        ${RESOLVER_CPP}
        )

target_include_directories(AstraMail_resolver PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        )

