#pragma once
#include <string>
#include <vector>
#include <climits>
#include <unistd.h>

namespace fsu
{
    // каталог, где лежит исполняемый файл (Linux/Astra: /proc/self/exe)
    std::string executableDir();

    bool ensureDir(const std::string& path);

    std::string getExecutableDir();

    // mkdir -p для пути (без std::filesystem, без исключений)
    bool ensureDirTree(const std::string& path, std::string& err);

    // запись бинарника
    bool writeBinaryFile(const std::string& path,
                         const std::vector<unsigned char>& data,
                         std::string& err);

    // простая склейка пути (без нормализации)
    std::string join(const std::string& a, const std::string& b);
}
