#include "FsUtils.h"

#include <unistd.h>
#include <sys/stat.h>
#include <fstream>

namespace fsu
{

    std::string getExecutableDir()
    {
        char buf[PATH_MAX] = {0};
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len <= 0)
            return ".";

        std::string path(buf, static_cast<size_t>(len));

        auto pos = path.find_last_of('/');
        if (pos == std::string::npos)
            return ".";

        return path.substr(0, pos);
    }



    bool ensureDir(const std::string& path)
    {
        if (::mkdir(path.c_str(), 0755) == 0)
            return true;

        return errno == EEXIST;
    }


    std::string executableDir()
    {
        char buf[PATH_MAX] = {0};
        ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len <= 0)
            return ".";

        std::string path(buf, static_cast<size_t>(len));
        auto pos = path.find_last_of('/');
        return (pos == std::string::npos) ? "." : path.substr(0, pos);
    }

    std::string join(const std::string& a, const std::string& b)
    {
        if (a.empty()) return b;
        if (b.empty()) return a;
        if (a.back() == '/') return a + b;
        return a + "/" + b;
    }

    static bool mkdirOne(const std::string& p, std::string& err)
    {
        if (::mkdir(p.c_str(), 0755) == 0)
            return true;

        if (errno == EEXIST)
            return true;

        err = "mkdir failed: " + p;
        return false;
    }

    bool ensureDirTree(const std::string& path, std::string& err)
    {
        err.clear();
        if (path.empty())
        {
            err = "empty path";
            return false;
        }

        std::string cur;
        size_t i = 0;

        // абсолютный путь
        if (!path.empty() && path[0] == '/')
        {
            cur = "/";
            i = 1;
        }

        while (i < path.size())
        {
            // пропускаем лишние '/'
            while (i < path.size() && path[i] == '/')
                ++i;

            if (i >= path.size())
                break;

            size_t j = path.find('/', i);
            if (j == std::string::npos)
                j = path.size();

            std::string part = path.substr(i, j - i);
            if (!part.empty())
            {
                if (cur.empty() || cur == "/") {
                    cur += part;
                } else {
                    cur.push_back('/');
                    cur += part;
                }

                if (!mkdirOne(cur, err))
                    return false;
            }

            i = j;
        }

        return true;
    }

    bool writeBinaryFile(const std::string& path,
                         const std::vector<unsigned char>& data,
                         std::string& err)
    {
        err.clear();

        std::ofstream f(path, std::ios::binary);
        if (!f)
        {
            err = "cannot open file for write: " + path;
            return false;
        }

        f.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));

        if (!f)
        {
            err = "write failed: " + path;
            return false;
        }

        return true;
    }
}
