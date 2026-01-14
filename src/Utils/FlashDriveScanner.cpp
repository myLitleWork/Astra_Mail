#include "FlashDriveScanner.h"

#include <fstream>
#include <cstdio>
#include <algorithm>
#include <iostream>
#include <unistd.h>   // getuid()

using namespace std;

FlashDriveScanner::FlashDriveScanner()
= default;

bool FlashDriveScanner::isUsbDisk(const std::string& disk)
{
    std::string sys = "/sys/block/" + disk + "/removable";
    std::ifstream f(sys);

    if (!f.is_open())
        return false;

    int v = 0;
    f >> v;
    return (v == 1); // USB флешки → removable = 1
}

vector<string> FlashDriveScanner::listPartitions(const string& disk)
{
    vector<string> parts;

    for (int i = 0; i < 10; i++)
    {
        string p = "/dev/" + disk + to_string(i);
        if (fs::exists(p))
            parts.push_back(p);
    }
    return parts;
}

// Выполнение shell-команды и получение вывода
static string execCmd(const string& cmd)
{
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe)
        return "";

    char buf[256];
    string result;

    while (fgets(buf, sizeof(buf), pipe))
        result += buf;

    pclose(pipe);

    // убираем \n
    if (!result.empty() && result.back() == '\n')
        result.pop_back();

    return result;
}

string FlashDriveScanner::getMountPoint(const string& part)
{
    string cmd = "findmnt -rn -S " + part + " -o TARGET 2>/dev/null";
    return execCmd(cmd);
}

string FlashDriveScanner::getLabel(const string& part)
{
    string cmd = "lsblk -no LABEL " + part + " 2>/dev/null";
    string r = execCmd(cmd);
    return r.empty() ? "(no-label)" : r;
}

string FlashDriveScanner::getFsType(const string& part)
{
    string cmd = "lsblk -no FSTYPE " + part + " 2>/dev/null";
    return execCmd(cmd);
}

uint64_t FlashDriveScanner::getSize(const string& part)
{
    string cmd = "blockdev --getsize64 " + part + " 2>/dev/null";
    string r = execCmd(cmd);
    if (r.empty())
        return 0;

    return std::stoull(r);
}

string FlashDriveScanner::getUuid(const string& part)
{
    string cmd = "blkid -s UUID -o value " + part + " 2>/dev/null";
    return execCmd(cmd);
}

void FlashDriveScanner::scan()
{
    drives.clear();

    // Ищем /dev/sdX
    for (const auto& d : fs::directory_iterator("/sys/block"))
    {
        string name = d.path().filename();
        if (name.rfind("sd", 0) != 0)
            continue;

        if (!isUsbDisk(name))
            continue;

        auto parts = listPartitions(name);

        for (const auto& p : parts)
        {
            FlashDriveInfo info;
            info.device = p;
            info.mountPoint = getMountPoint(p);
            info.label = getLabel(p);
            info.fsType = getFsType(p);
            info.sizeBytes = getSize(p);
            info.uuid = getUuid(p);
            info.mounted = !info.mountPoint.empty();

            if (!info.uuid.empty())
            {
                info.id = "uuid:" + info.uuid;
            }
            else
            {
                // fallback — для VMware / FAT / GUI mount
                info.id = "dev:" + info.device + ":" + std::to_string(info.sizeBytes);
            }

            auto it = std::find_if(
                    drives.begin(), drives.end(),
                    [&](const FlashDriveInfo& d)
                    {
                        return d.id == info.id;
                    }
            );


            if (it != drives.end())
            {
                // Уже есть такая флешка — обновляем состояние
                it->mounted    = info.mounted;
                it->mountPoint = info.mountPoint;
                it->device     = info.device;
                continue;
            }

            drives.push_back(info);
        }
    }
}

// Определение: мусорный каталог автомонтирования
bool FlashDriveScanner::isGarbageDir(const fs::path& p)
{
    string s = p.filename().string();

    // каталог типа by-uuid-68CD-EA7B_________________
    if (s.rfind("by-uuid-", 0) == 0)
        return true;

    // пустой каталог
    if (fs::is_directory(p))
    {
        if (fs::is_empty(p))
            return true;
    }

    return false;
}



void FlashDriveScanner::cleanupGarbageMounts()
{
    uid_t uid = getuid();   // ← вот так правильно
    std::string baseDir = "/run/user/" + std::to_string(uid) + "/media";

    std::error_code ec;

    std::cout << "[cleanup] UID=" << uid << "\n";
    std::cout << "[cleanup] baseDir=" << baseDir << "\n";

    if (!fs::exists(baseDir, ec) || !fs::is_directory(baseDir, ec))
    {
        std::cout << "[cleanup] Каталог не существует\n";
        return;
    }

    for (const auto& entry : fs::directory_iterator(baseDir, ec))
    {
        if (ec) break;
        if (!fs::is_directory(entry, ec)) continue;

        const auto& p = entry.path();
        if (!isGarbageDir(p)) continue;

        std::cout << "[cleanup] мусор: " << p << "\n";

        execCmd("umount " + p.string() + " 2>/dev/null");

        fs::remove_all(p, ec);
        if (ec)
            std::cout << "[cleanup] ошибка удаления: " << ec.message() << "\n";
        else
            std::cout << "[cleanup] удалён\n";
    }
}
