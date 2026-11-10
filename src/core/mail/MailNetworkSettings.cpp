#include "MailNetworkSettings.h"
#include "Configs/Confg/ConfigPaths.h"
#include "Configs/Confg/ConfigBootstrap.h"
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace astra_mail {
net::Settings loadMailNetworkSettings() {
    config::ensureNetworkSettings(false);
    net::Settings settings;
    std::unordered_map<std::string, unsigned*> seconds{
        {"dns_seconds", &settings.dnsMs}, {"connect_seconds", &settings.connectMs},
        {"smtp_command_seconds", &settings.smtpCommandMs}, {"smtp_data_start_seconds", &settings.smtpDataStartMs},
        {"smtp_final_seconds", &settings.smtpFinalMs}, {"io_idle_seconds", &settings.ioIdleMs},
        {"imap_command_seconds", &settings.imapCommandMs}, {"imap_append_seconds", &settings.imapAppendMs},
        {"tcp_user_seconds", &settings.tcpUserMs}};
    std::ifstream file(config::networkSettings().string());
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto split = line.find('=');
        if (split == std::string::npos) continue;
        std::string key = line.substr(0, split);
        const auto end = key.find_last_not_of(" \t\r");
        if (end == std::string::npos) continue;
        key.erase(end + 1); key.erase(0, key.find_first_not_of(" \t"));
        std::istringstream value(line.substr(split + 1));
        unsigned parsed = 0; if (!(value >> parsed) || !parsed || parsed > 86400) continue;
        value >> std::ws; if (!value.eof()) continue;
        const auto it = seconds.find(key);
        if (it != seconds.end()) *it->second = parsed * 1000u;
        else if (key == "keep_idle_seconds") settings.keepIdleSeconds = parsed;
        else if (key == "keep_interval_seconds") settings.keepIntervalSeconds = parsed;
        else if (key == "keep_probes" && parsed <= 10) settings.keepProbes = parsed;
    }
    return settings;
}
}
