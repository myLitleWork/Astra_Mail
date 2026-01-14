#pragma once
#include <string>
#include <vector>

struct EmailAttachment {
    std::string filename;
    std::string path;   // путь к файлу
};


struct EmailMessage
{
    std::string from;
    std::string to;
    std::string subject;
    std::string body;

    std::vector<EmailAttachment> attachments;
};
