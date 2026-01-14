#include "EditorLauncher.h"
#include <cstdlib>
#include <unistd.h>

namespace astra_mail::system
{
    bool tryEditor(const char* editor, const fs::path& file)
    {
        if (!editor) return false;

        std::string cmd = std::string(editor) + " \"" + file.string() + "\"";
        return std::system(cmd.c_str()) == 0;
    }

    bool openInEditor(const fs::path& file,
                      std::string& err)
    {
        if (!fs::exists(file))
        {
            err = "Файл не существует: " + file.string();
            return false;
        }

        // порядок fallback — профессиональный
        if (tryEditor(std::getenv("EDITOR"), file)) return true;
        if (tryEditor("nano", file)) return true;
        if (tryEditor("vi", file))   return true;
        if (tryEditor("less", file)) return true;

        err = "Не найден ни один текстовый редактор";
        return false;
    }
}
