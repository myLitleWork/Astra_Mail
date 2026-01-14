#pragma once
#include <string>
#include <vector>
#if __has_include(<filesystem>)
#include <filesystem>
  namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif


struct FlashDriveInfo
{
    std::string device;       // /dev/sdb1
    std::string label;        // KINGSTON
    std::string mountPoint;   // /run/media/sg/UUID
    std::string fsType;       // vfat, exfat, ntfs
    uint64_t sizeBytes = 0;   // 15662333952
    std::string uuid;         // 68CD-EA7B
    std::string id; // стабильный идентификатор
    bool mounted = false;
};

class FlashDriveScanner
{
public:
    FlashDriveScanner();

    // Основная функция
    void scan();

    [[nodiscard]] const std::vector<FlashDriveInfo>& getDrives() const { return drives; }

    // Удаление мусорных каталогов автомонтирования
    static void cleanupGarbageMounts();

private:
    std::vector<FlashDriveInfo> drives;

    static bool isUsbDisk(const std::string& disk);
    static std::vector<std::string> listPartitions(const std::string& disk);
    static std::string getMountPoint(const std::string& part);
    static std::string getLabel(const std::string& part);
    static std::string getFsType(const std::string& part);
    static uint64_t getSize(const std::string& part);
    static std::string getUuid(const std::string& part);

    static bool isGarbageDir(const fs::path& p);
};
