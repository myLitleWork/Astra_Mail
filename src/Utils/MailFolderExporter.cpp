#include "MailFolderExporter.h"
#include "Utils/EmlParser.h"
#include "Base64.h"
#include "UtilsFS/FsUtils.h"
#include "SubjectUtils.h"

bool MailFolderExporter::exportFromRaw(const std::string& rawEml,
                                       const std::string& mailId,
                                       std::string& err)
{
    std::string topHeaders, topBody;
    EmlParser::extractTopHeaders(rawEml, topHeaders, topBody);

    std::string subject = EmlParser::extractSubject(topHeaders);
    std::string topicDir = baseOutDir_;

    if (hasFlag(flags_, MailSaveFlags::SortByTopic))
    {
        topicDir = fsu::join(baseOutDir_,
                             normalizeSubjectToDir(subject));
    }

    std::string mailDir = fsu::join(topicDir, mailId);

    if (!fsu::ensureDirTree(mailDir, err))
        return false;

    // сохранить raw.eml
    if (hasFlag(flags_, MailSaveFlags::SaveEml))
    {
        std::vector<unsigned char> data(rawEml.begin(), rawEml.end());
        fsu::writeBinaryFile(
                fsu::join(mailDir, "raw.eml"), data, err);
    }

    // вложения
    if (hasFlag(flags_, MailSaveFlags::SaveAttach))
    {
        std::vector<MimePart> parts;
        if (!EmlParser::splitToPartsFromRaw(rawEml, parts, err))
            return false;

        for (const auto& p : parts)
        {
            ParsedPart pp = EmlParser::parsePart(p);
            if (!pp.isAttachment)
                continue;

            if (pp.transferEncoding != "base64")
                continue;

            auto data = decodeBase64(pp.body);
            fsu::writeBinaryFile(
                    fsu::join(mailDir, pp.filename),
                    data,
                    err);
        }
    }

    return true;
}

MailFolderExporter::MailFolderExporter(std::string baseOutDir, MailSaveFlags flags) {

}
