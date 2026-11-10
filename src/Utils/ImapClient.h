#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include <netdb.h>
#include <unistd.h>
#include "MailTransport.h"

struct ImapSession
{
    astra_mail::net::Socket sock;
    int nextTag = 1;   // 👈 ДОБАВИТЬ
};

bool imapOpen(ImapSession& ses,
              const std::string& host,
              int port,
              std::string& err);

bool imapLogin(ImapSession& ses,
               const std::string& user,
               const std::string& pass,
               std::string& err);

bool imapSelect(ImapSession& ses,
                const std::string& folder,
                std::string& err);

void imapClose(ImapSession& ses);

    class ImapClient
    {

        bool verbose_;
    public:


        static bool searchUnreadUids(ImapSession& ses,
                              const std::string& folder,
                              std::vector<std::string>& outUids,
                              std::string& err);

        static bool searchUnreadUids(ImapSession& ses,
                                     const std::vector<std::string>& folders,
                                     std::vector<std::string>& outUids,
                                     std::string& err);

        static bool selectMailbox(ImapSession& ses,
                                  const std::string& folder,
                                  std::string& err);

        static bool listMailboxes(ImapSession& ses,
                                  std::vector<std::string>& outFolders,
                                  std::string& err);

        static bool listMailboxes(const std::string& host,
                                  int port,
                                  const std::string& user,
                                  const std::string& pass,
                                  std::vector<std::string>& outFolders,
                                  std::string& err);



        static bool fetchRawEmlByUid(ImapSession& ses,
                              const std::string& uid,
                              std::string& outRawEml,
                              std::string& err);

        static bool markSeenByUid(ImapSession& ses,
                           const std::string& uid,
                           std::string& err);





        static bool searchUnreadUids(const std::string& host,
                              int port,
                              const std::string& user,
                              const std::string& pass,
                              const std::string& folder, // обычно "INBOX"
                              std::vector<std::string>& outUids,
                              std::string& err);

        static bool searchUnreadUids(const std::string& host,
                                     int port,
                                     const std::string& user,
                                     const std::string& pass,
                                     const std::vector<std::string>& folders,
                                     std::vector<std::string>& outUids,
                                     std::string& err);

        static bool fetchRawEmlByUid(const std::string& host,
                              int port,
                              const std::string& user,
                              const std::string& pass,
                              const std::string& folder, // "INBOX"
                              const std::string& uid,
                              std::string& outRawEml,
                              std::string& err);

        static bool markSeenByUid(const std::string& host,
                           int port,
                           const std::string& user,
                           const std::string& pass,
                           const std::string& folder, // "INBOX"
                           const std::string& uid,
                           std::string& err);


        static bool listFolders(const std::string& host,
                                int port,
                                const std::string& user,
                                const std::string& pass,
                                std::string& outResult,
                                std::string& outError);



        bool imapAppendSentFromFile(
                const std::string& mimeFile,
                std::string& err);
        bool imapAppendSentFromStream(FILE* mime, std::string& err);


        bool imapAppendSent(const std::string& rawMime, std::string& err);

        explicit ImapClient(bool _verbose) : verbose_(_verbose){}
    };
