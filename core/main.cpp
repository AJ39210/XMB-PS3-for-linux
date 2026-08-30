#include "raylib.h"
#include "services/xml_parser.h"
#include <vector>
#include <string>
#include <cstdio>
#include <iostream>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <dirent.h>
#include <algorithm>
#include <set>
#include <map>
#include <fstream>
#include <cctype>
#include <cstdlib>
#include <ctime>

enum BootState {
    STATE_BOOT_INTRO = 0,
    STATE_XMB_RUNNING
};

enum NetType {
    NET_OFFLINE = 0,
    NET_WIFI = 1,
    NET_WIRED = 2
};

static inline float ClampF(float value, float minVal, float maxVal) {
    if (value < minVal) return minVal;
    if (value > maxVal) return maxVal;
    return value;
}
static inline int ClampI(int value, int minVal, int maxVal) {
    if (value < minVal) return minVal;
    if (value > maxVal) return maxVal;
    return value;
}

struct AppEntry {
    std::string name;
    std::string exec;
    std::string categoryBucket;
};

static std::string TrimString(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string XmlEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            default:   out += c;
        }
    }
    return out;
}

static std::string Slugify(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (std::isalnum((unsigned char)c)) out += (char)std::tolower((unsigned char)c);
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "other" : out;
}

static std::string CleanExecString(const std::string& rawExec) {
    std::vector<std::string> tokens;
    std::string cur;
    bool inQuotes = false;

    for (char c : rawExec) {
        if (c == '"') {
            inQuotes = !inQuotes;
            cur += c;
            continue;
        }
        if (c == ' ' && !inQuotes) {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) tokens.push_back(cur);

    std::string result;
    for (auto& t : tokens) {
        if (!t.empty() && t[0] == '%') continue;
        if (!result.empty()) result += ' ';
        result += t;
    }
    return result;
}

static std::string CategoryBucket(const std::string& categoriesRaw) {
    auto has = [&](const std::string& cat) {
        size_t pos = 0;
        while (true) {
            pos = categoriesRaw.find(cat, pos);
            if (pos == std::string::npos) return false;
            bool leftOk = (pos == 0) || (categoriesRaw[pos - 1] == ';');
            size_t endPos = pos + cat.size();
            bool rightOk = (endPos == categoriesRaw.size()) || (categoriesRaw[endPos] == ';');
            if (leftOk && rightOk) return true;
            pos = endPos;
        }
    };

    if (has("Wine")) return "Wine";
    if (has("Game")) return "Games";
    if (has("WebBrowser")) return "Web";
    if (has("Network") || has("Email") || has("InstantMessaging") || has("Chat")) return "Internet";
    if (has("Development")) return "Programming";
    if (has("Graphics")) return "Graphics";
    if (has("AudioVideo") || has("Audio") || has("Video")) return "Sound & Video";
    if (has("Office")) return "Office";
    if (has("Settings")) return "Preferences";
    if (has("System")) return "Administration";
    if (has("Utility") || has("Accessibility")) return "Accessories";
    return "Other";
}

static bool ParseDesktopFile(const std::string& path, AppEntry& outEntry) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    bool inEntry = false;
    bool sawType = false;
    bool typeIsApplication = false;
    bool noDisplay = false;
    bool hidden = false;
    std::string name, execRaw, categoriesRaw;

    while (std::getline(file, line)) {
        std::string trimmed = TrimString(line);
        if (trimmed.empty()) continue;

        if (trimmed[0] == '[') {
            inEntry = (trimmed == "[Desktop Entry]");
            continue;
        }
        if (!inEntry) continue;

        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;

        std::string key = TrimString(trimmed.substr(0, eq));
        std::string value = TrimString(trimmed.substr(eq + 1));

        if (key == "Name" && name.empty()) {
            name = value;
        } else if (key == "Exec" && execRaw.empty()) {
            execRaw = value;
        } else if (key == "Categories" && categoriesRaw.empty()) {
            categoriesRaw = value;
        } else if (key == "Type") {
            sawType = true;
            typeIsApplication = (value == "Application");
        } else if (key == "NoDisplay") {
            noDisplay = (value == "true");
        } else if (key == "Hidden") {
            hidden = (value == "true");
        }
    }

    if (sawType && !typeIsApplication) return false;
    if (noDisplay || hidden) return false;
    if (name.empty() || execRaw.empty()) return false;

    outEntry.name = name;
    outEntry.exec = CleanExecString(execRaw);
    outEntry.categoryBucket = CategoryBucket(categoriesRaw);
    if (outEntry.exec.empty()) return false;

    return true;
}

static std::vector<AppEntry> ScanInstalledApps() {
    std::vector<std::string> dirs = {
        "/usr/share/applications",
        "/usr/local/share/applications",
        "/var/lib/snapd/desktop/applications",
        "/var/lib/flatpak/exports/share/applications"
    };

    const char* home = getenv("HOME");
    if (home) {
        dirs.push_back(std::string(home) + "/.local/share/applications");
        dirs.push_back(std::string(home) + "/.local/share/flatpak/exports/share/applications");
    }

    std::vector<AppEntry> apps;
    std::set<std::string> seenNames;

    for (const auto& dirPath : dirs) {
        DIR* dir = opendir(dirPath.c_str());
        if (!dir) continue;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string fname = entry->d_name;
            if (fname.size() < 8) continue;
            if (fname.compare(fname.size() - 8, 8, ".desktop") != 0) continue;

            std::string fullPath = dirPath + "/" + fname;
            AppEntry e;
            if (ParseDesktopFile(fullPath, e)) {
                if (seenNames.find(e.name) == seenNames.end()) {
                    seenNames.insert(e.name);
                    apps.push_back(e);
                }
            }
        }
        closedir(dir);
    }

    std::sort(apps.begin(), apps.end(), [](const AppEntry& a, const AppEntry& b) {
        std::string an = a.name, bn = b.name;
        std::transform(an.begin(), an.end(), an.begin(), ::tolower);
        std::transform(bn.begin(), bn.end(), bn.begin(), ::tolower);
        return an < bn;
    });

    return apps;
}

static void WriteMenuXml(const std::string& path, const std::vector<AppEntry>& apps) {
    std::ofstream out(path);
    if (!out.is_open()) {
        std::cerr << "[XMB] Failed to write " << path << std::endl;
        return;
    }

    std::map<std::string, std::vector<AppEntry>> grouped;
    for (const auto& app : apps) {
        grouped[app.categoryBucket].push_back(app);
    }

    out << "<XMB>\n";
    for (const auto& kv : grouped) {
        const std::string& bucketName = kv.first;
        const std::vector<AppEntry>& bucketApps = kv.second;
        if (bucketApps.empty()) continue;

        out << "    <Column id=\"" << Slugify(bucketName) << "\" title=\"" << XmlEscape(bucketName) << "\">\n";
        for (const auto& app : bucketApps) {
            out << "        <Item name=\"" << XmlEscape(app.name)
                << "\" exec=\"" << XmlEscape(app.exec) << " &amp;\" />\n";
        }
        out << "    </Column>\n";
    }

    out << "    <Column id=\"system\" title=\"System\">\n";
    out << "        <Item name=\"Exit XMB\" exec=\"killall xmb_desktop\" />\n";
    out << "    </Column>\n";
    out << "</XMB>\n";
}

struct BatteryStatus {
    int percent = -1;
    bool charging = false;
    bool full = false;
};

static BatteryStatus ReadBatteryStatus() {
    BatteryStatus result;
    std::vector<std::string> candidates = { "BAT0", "BAT1" };

    DIR* dir = opendir("/sys/class/power_supply");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.rfind("BAT", 0) == 0 &&
                std::find(candidates.begin(), candidates.end(), name) == candidates.end()) {
                candidates.push_back(name);
            }
        }
        closedir(dir);
    }

    for (const auto& bat : candidates) {
        std::string base = "/sys/class/power_supply/" + bat + "/";
        std::ifstream capFile(base + "capacity");
        if (!capFile.good()) continue;

        int cap = -1;
        capFile >> cap;
        if (cap < 0) continue;

        std::string status;
        std::ifstream statusFile(base + "status");
        if (statusFile.good()) statusFile >> status;

        result.percent = ClampI(cap, 0, 100);
        result.charging = (status == "Charging");
        result.full = (status == "Full");
        return result;
    }

    return result;
}

struct NetworkStatus {
    int type = NET_OFFLINE;
    int wifiSignal = 0;
};

static NetworkStatus ReadNetworkStatus() {
    NetworkStatus result;

    FILE* pipe = popen("nmcli -t -f TYPE,STATE dev status 2>/dev/null", "r");
    if (!pipe) return result;

    bool sawWiredConnected = false;
    bool sawWifiConnected = false;
    char lineBuf[256];
    while (fgets(lineBuf, sizeof(lineBuf), pipe)) {
        std::string line(lineBuf);
        if (line.find("ethernet:connected") != std::string::npos) sawWiredConnected = true;
        if (line.find("wifi:connected") != std::string::npos) sawWifiConnected = true;
    }
    pclose(pipe);

    if (sawWiredConnected) {
        result.type = NET_WIRED;
        return result;
    }
    if (sawWifiConnected) {
        result.type = NET_WIFI;
        FILE* sigPipe = popen("nmcli -t -f active,signal dev wifi 2>/dev/null", "r");
        if (sigPipe) {
            char sigLine[128];
            while (fgets(sigLine, sizeof(sigLine), sigPipe)) {
                std::string s(sigLine);
                if (s.rfind("yes:", 0) == 0) {
                    result.wifiSignal = atoi(s.c_str() + 4);
                    break;
                }
            }
            pclose(sigPipe);
        }
        return result;
    }

    result.type = NET_OFFLINE;
    return result;
}

static std::string BatteryIconFilename(int percent, bool charging, bool full) {
    if (percent < 0) return "";

    if (full) return "battery-full-charged-symbolic.png";
    if (percent >= 100) return charging ? "battery-level-100-charged-symbolic.png" : "battery-level-100-symbolic.png";

    int level = ((percent + 5) / 10) * 10;
    level = ClampI(level, 0, 90);
    return "battery-level-" + std::to_string(level) + (charging ? "-charging-symbolic.png" : "-symbolic.png");
}

static std::string NetworkIconFilename(int netType, int wifiSignal) {
    if (netType == NET_WIRED) return "wire-pluged-in.png";
    if (netType == NET_WIFI) {
        if (wifiSignal >= 80) return "network-wireless-signal-excellent-symbolic.png";
        if (wifiSignal >= 60) return "network-wireless-signal-good-symbolic.png";
        if (wifiSignal >= 35) return "network-wireless-signal-ok-symbolic.png";
        if (wifiSignal >= 1)  return "network-wireless-signal-weak-symbolic.png";
        return "network-wireless-signal-none-symbolic.png";
    }
    return "network-wireless-offline-symbolic.png";
}

int main() {
    const int screenWidth = 1920;
    const int screenHeight = 1080;
    
    InitWindow(screenWidth, screenHeight, "PS3 XMB Desktop");
    SetExitKey(KEY_NULL);
    InitAudioDevice();
    SetTargetFPS(60);

    Font xmbFont = LoadFont("Icons/systematic/font.ttf");

    std::map<std::string, Texture2D> statusIcons;
    {
        std::vector<std::string> batteryFiles = {
            "battery-full-charged-symbolic.png",
            "battery-level-0-charging-symbolic.png", "battery-level-0-symbolic.png",
            "battery-level-100-charged-symbolic.png", "battery-level-100-symbolic.png",
            "battery-level-10-charging-symbolic.png", "battery-level-10-symbolic.png",
            "battery-level-20-charging-symbolic.png", "battery-level-20-symbolic.png",
            "battery-level-30-charging-symbolic.png", "battery-level-30-symbolic.png",
            "battery-level-40-charging-symbolic.png", "battery-level-40-symbolic.png",
            "battery-level-50-charging-symbolic.png", "battery-level-50-symbolic.png",
            "battery-level-60-charging-symbolic.png", "battery-level-60-symbolic.png",
            "battery-level-70-charging-symbolic.png", "battery-level-70-symbolic.png",
            "battery-level-80-charging-symbolic.png", "battery-level-80-symbolic.png",
            "battery-level-90-charging-symbolic.png", "battery-level-90-symbolic.png"
        };
        std::vector<std::string> ethernetFiles = {
            "network-wireless-offline-symbolic.png",
            "network-wireless-signal-excellent-symbolic.png",
            "network-wireless-signal-good-symbolic.png",
            "network-wireless-signal-none-symbolic.png",
            "network-wireless-signal-ok-symbolic.png",
            "network-wireless-signal-weak-symbolic.png",
            "wire-pluged-in.png"
        };

        for (const auto& f : batteryFiles) {
            Texture2D tex = LoadTexture(("Icons/battery/" + f).c_str());
            SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
            statusIcons[f] = tex;
        }
        for (const auto& f : ethernetFiles) {
            Texture2D tex = LoadTexture(("Icons/ethernet/" + f).c_str());
            SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
            statusIcons[f] = tex;
        }
    }

    Sound coldboot = LoadSound("sound/coldboot_stereo.wav");
    Sound sndOk = LoadSound("sound/snd_system_ok.wav");
    Sound sndCancel = LoadSound("sound/snd_cancel.wav");
    Sound sndOption = LoadSound("sound/snd_option.wav");

    PlaySound(coldboot);

    {
        std::ifstream existingMenu("menu.xml");
        if (!existingMenu.good()) {
            std::cout << "[XMB] No menu.xml found -- scanning installed applications (first boot)..." << std::endl;
            std::vector<AppEntry> apps = ScanInstalledApps();
            WriteMenuXml("menu.xml", apps);
            std::cout << "[XMB] Found " << apps.size()
                      << " apps -> wrote categorized menu.xml (this is now the default; delete menu.xml to rescan)" << std::endl;
        }
    }
    std::vector<XMBColumn> menuColumns = XMLParser::LoadMenuConfig("menu.xml");

    std::map<std::string, Texture2D> iconCache;
    {
        std::set<std::string> iconPaths;
        for (const auto& col : menuColumns) {
            if (!col.icon.empty()) iconPaths.insert(col.icon);
            for (const auto& f : col.folders) {
                if (!f.icon.empty()) iconPaths.insert(f.icon);
            }
        }
        for (const auto& p : iconPaths) {
            Texture2D tex = LoadTexture(p.c_str());
            SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
            iconCache[p] = tex;
        }
    }
    int currentColumn = 0;
    int currentRow = 0;
    int currentFolder = -1;

    float columnAnim = 0.0f;
    float rowAnim = 0.0f;

    BootState state = STATE_BOOT_INTRO;
    float bootTimer = 0.0f;
    float bootAlpha = 0.0f; 
    const float bootDuration = 6.5f; 

    std::atomic<int>  battPercent{-1};
    std::atomic<bool> battCharging{false};
    std::atomic<bool> battFull{false};
    std::atomic<int>  netType{NET_OFFLINE};
    std::atomic<int>  wifiSignal{0};
    std::atomic<bool> statusThreadRunning{true};

    std::thread statusThread([&]() {
        while (statusThreadRunning.load()) {
            BatteryStatus b = ReadBatteryStatus();
            battPercent.store(b.percent);
            battCharging.store(b.charging);
            battFull.store(b.full);

            NetworkStatus n = ReadNetworkStatus();
            netType.store(n.type);
            wifiSignal.store(n.wifiSignal);

            for (int i = 0; i < 10 && statusThreadRunning.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    });

    const int vidWidth = 640;
    const int vidHeight = 360;
    
    std::string ffmpegCmd = "ffmpeg -v error -threads 0 -re -stream_loop -1 -i 'background/bg_640x360.mp4' -an -sn -f rawvideo -pix_fmt rgb24 -r 30 -";
    
    std::cout << "[XMB] Launching background video stream..." << std::endl;
    FILE* ffmpegPipe = popen(ffmpegCmd.c_str(), "r");
    
    bool useVideoBg = false;
    size_t frameSize = vidWidth * vidHeight * 3;

    std::vector<unsigned char> pendingFrame(frameSize, 0);
    std::mutex frameMutex;
    std::atomic<bool> newFrameReady{false};
    std::atomic<bool> decodeThreadRunning{false};
    std::thread decodeThread;

    if (ffmpegPipe != nullptr) {
        useVideoBg = true;
        decodeThreadRunning = true;
        int fd = fileno(ffmpegPipe);

        decodeThread = std::thread([fd, frameSize, &pendingFrame, &frameMutex, &newFrameReady, &decodeThreadRunning]() {
            std::vector<unsigned char> localFrame(frameSize, 0);
            size_t got = 0;
            while (decodeThreadRunning.load()) {
                ssize_t n = read(fd, localFrame.data() + got, frameSize - got);
                if (n > 0) {
                    got += (size_t)n;
                    if (got >= frameSize) {
                        {
                            std::lock_guard<std::mutex> lock(frameMutex);
                            pendingFrame = localFrame;
                        }
                        newFrameReady.store(true);
                        got = 0;
                    }
                } else if (n == 0) {
                    break;
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            }
        });

        std::cout << "[XMB] Video stream active!" << std::endl;
    }

    Image bgImage = { 0 };
    bgImage.width = vidWidth;
    bgImage.height = vidHeight;
    bgImage.mipmaps = 1;
    bgImage.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
    bgImage.data = std::calloc(1, (size_t)vidWidth * vidHeight * 3);

    unsigned char* fillPx = reinterpret_cast<unsigned char*>(bgImage.data);
    for (int p = 0; p < vidWidth * vidHeight; ++p) {
        fillPx[p * 3 + 0] = 0x0a;
        fillPx[p * 3 + 1] = 0x22;
        fillPx[p * 3 + 2] = 0x3b;
    }

    Texture2D bgVideoTexture = LoadTextureFromImage(bgImage);
    UnloadImage(bgImage);
    SetTextureFilter(bgVideoTexture, TEXTURE_FILTER_BILINEAR);

    float waveTimer = 0.0f;
    std::vector<unsigned char> displayFrame(frameSize, 0);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        waveTimer += dt * 1.5f;

        if (IsKeyPressed(KEY_F11)) {
            ToggleFullscreen();
        }

        const float animSpeed = 14.0f;
        float animT = 1.0f - expf(-animSpeed * dt);
        columnAnim += ((float)currentColumn - columnAnim) * animT;
        rowAnim += ((float)currentRow - rowAnim) * animT;

        if (useVideoBg && state == STATE_XMB_RUNNING && newFrameReady.load()) {
            {
                std::lock_guard<std::mutex> lock(frameMutex);
                displayFrame = pendingFrame;
            }
            newFrameReady.store(false);
            UpdateTexture(bgVideoTexture, displayFrame.data());
        }

        if (state == STATE_BOOT_INTRO) {
            bootTimer += dt;
            
            if (bootTimer < 3.0f) {
                bootAlpha += 0.01f;
                if (bootAlpha > 1.0f) bootAlpha = 1.0f;
            } else if (bootTimer > (bootDuration - 1.5f)) {
                bootAlpha -= 0.02f;
                if (bootAlpha < 0.0f) bootAlpha = 0.0f;
            }

            if (bootTimer >= bootDuration || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
                state = STATE_XMB_RUNNING;
                bootAlpha = 1.0f;
            }
        } 
        else {
            if (!menuColumns.empty()) {
                XMBColumn& col = menuColumns[currentColumn];
                bool hasFolders = !col.folders.empty();
                bool browsingFolderList = hasFolders && currentFolder == -1;
                bool insideFolder = hasFolders && currentFolder != -1;

                size_t rowCount = 0;
                if (browsingFolderList) rowCount = col.folders.size();
                else if (insideFolder) rowCount = col.folders[currentFolder].items.size();
                else rowCount = col.items.size();

                if (IsKeyPressed(KEY_RIGHT)) {
                    currentColumn = (currentColumn + 1) % menuColumns.size();
                    currentRow = 0;
                    currentFolder = -1;
                    PlaySound(sndOption);
                }
                if (IsKeyPressed(KEY_LEFT)) {
                    currentColumn = (currentColumn - 1 + menuColumns.size()) % menuColumns.size();
                    currentRow = 0;
                    currentFolder = -1;
                    PlaySound(sndOption);
                }
                if (IsKeyPressed(KEY_DOWN)) {
                    if (rowCount > 0) {
                        currentRow = (currentRow + 1) % rowCount;
                        PlaySound(sndOption);
                    }
                }
                if (IsKeyPressed(KEY_UP)) {
                    if (rowCount > 0) {
                        currentRow = (currentRow - 1 + rowCount) % rowCount;
                        PlaySound(sndOption);
                    }
                }
                if (IsKeyPressed(KEY_BACKSPACE)) {
                    if (insideFolder) {
                        currentFolder = -1;
                        currentRow = 0;
                        PlaySound(sndOption);
                    }
                }
                if (IsKeyPressed(KEY_ENTER)) {
                    if (browsingFolderList) {
                        currentFolder = currentRow;
                        currentRow = 0;
                        PlaySound(sndOption);
                    } else if (rowCount > 0) {
                        std::string cmd = insideFolder
                            ? col.folders[currentFolder].items[currentRow].exec
                            : col.items[currentRow].exec;
                        system(cmd.c_str());
                    }
                }
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);

        if (state == STATE_BOOT_INTRO) {
            const char* creditLine1 = "PLAYSTATION 3 XMB ENVIRONMENT";
            const char* creditLine2 = "Linux Port by AJ (Predescu Ulariu)";
            const char* skipHint = "(Press Enter to Skip)";

            DrawTextEx(xmbFont, creditLine1, { (screenWidth - MeasureTextEx(xmbFont, creditLine1, 32, 1.0f).x) / 2, 450.0f }, 32, 1.0f, Fade(WHITE, bootAlpha));
            DrawTextEx(xmbFont, creditLine2, { (screenWidth - MeasureTextEx(xmbFont, creditLine2, 20, 1.0f).x) / 2, 510.0f }, 20, 1.0f, Fade(LIGHTGRAY, bootAlpha));
            DrawTextEx(xmbFont, skipHint, { (screenWidth - MeasureTextEx(xmbFont, skipHint, 14, 1.0f).x) / 2, 600.0f }, 14, 1.0f, Fade(DARKGRAY, bootAlpha * 0.5f));
        } 
        else {
            if (useVideoBg) {
                DrawTexturePro(bgVideoTexture, 
                    (Rectangle){ 0.0f, 0.0f, (float)vidWidth, (float)vidHeight }, 
                    (Rectangle){ 0.0f, 0.0f, (float)screenWidth, (float)screenHeight }, 
                    (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
            } else {
                DrawRectangleGradientV(0, 0, screenWidth, screenHeight, GetColor(0x0a223bff), GetColor(0x020a14ff));
            }

            DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, 0.3f));

            {
                time_t now = time(nullptr);
                struct tm* lt = localtime(&now);
                char timeBuf[8];
                snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", lt->tm_hour, lt->tm_min);

                float barCenterY = 30.0f;
                float rightEdge = screenWidth - 60.0f;
                float iconSize = 34.0f;
                float gap = 22.0f;

                Vector2 timeDims = MeasureTextEx(xmbFont, timeBuf, 26, 1.0f);
                float timeX = rightEdge - timeDims.x;
                float cursorX = timeX - gap;

                int bp = battPercent.load();
                if (bp >= 0) {
                    std::string battFile = BatteryIconFilename(bp, battCharging.load(), battFull.load());
                    auto it = statusIcons.find(battFile);
                    if (it != statusIcons.end() && it->second.id != 0) {
                        float x = cursorX - iconSize;
                        DrawTexturePro(it->second,
                            (Rectangle){ 0.0f, 0.0f, (float)it->second.width, (float)it->second.height },
                            (Rectangle){ x, barCenterY - iconSize / 2.0f, iconSize, iconSize },
                            (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
                        cursorX = x - gap;
                    }
                }

                std::string netFile = NetworkIconFilename(netType.load(), wifiSignal.load());
                auto netIt = statusIcons.find(netFile);
                if (netIt != statusIcons.end() && netIt->second.id != 0) {
                    float x = cursorX - iconSize;
                    DrawTexturePro(netIt->second,
                        (Rectangle){ 0.0f, 0.0f, (float)netIt->second.width, (float)netIt->second.height },
                        (Rectangle){ x, barCenterY - iconSize / 2.0f, iconSize, iconSize },
                        (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
                }

                DrawTextEx(xmbFont, timeBuf, { timeX, barCenterY - timeDims.y / 2.0f }, 26, 1.0f, WHITE);
            }

            if (!menuColumns.empty()) {
                float startX = 400.0f;
                for (size_t c = 0; c < menuColumns.size(); ++c) {
                    float delta = (float)c - columnAnim;
                    float t = 1.0f - ClampF(fabsf(delta), 0.0f, 1.0f);

                    Color colColor = Fade(WHITE, 0.4f + 0.6f * t);
                    float fontSize = 22.0f + 6.0f * t;

                    float xPos = startX + delta * 250.0f;

                    Vector2 titleDims = MeasureTextEx(xmbFont, menuColumns[c].title.c_str(), fontSize, 1.0f);
                    float iconSize = 48.0f + 16.0f * t;
                    float iconX = xPos + (titleDims.x / 2.0f) - (iconSize / 2.0f);
                    float iconY = 90.0f;

                    auto colIconIt = iconCache.find(menuColumns[c].icon);
                    if (colIconIt != iconCache.end() && colIconIt->second.id != 0) {
                        DrawTexturePro(colIconIt->second,
                            (Rectangle){ 0.0f, 0.0f, (float)colIconIt->second.width, (float)colIconIt->second.height },
                            (Rectangle){ iconX, iconY, iconSize, iconSize },
                            (Vector2){ 0.0f, 0.0f }, 0.0f, Fade(WHITE, 0.4f + 0.6f * t));
                    }

                    DrawTextEx(xmbFont, menuColumns[c].title.c_str(), { xPos, 170.0f }, fontSize, 1.0f, colColor);
                }

                XMBColumn& renderCol = menuColumns[currentColumn];
                bool renderHasFolders = !renderCol.folders.empty();
                bool renderBrowsingFolders = renderHasFolders && currentFolder == -1;
                bool renderInsideFolder = renderHasFolders && currentFolder != -1;

                std::vector<std::string> displayNames;
                if (renderBrowsingFolders) {
                    for (auto& f : renderCol.folders) displayNames.push_back(f.name);
                } else if (renderInsideFolder) {
                    for (auto& it : renderCol.folders[currentFolder].items) displayNames.push_back(it.name);
                } else {
                    for (auto& it : renderCol.items) displayNames.push_back(it.name);
                }

                if (renderInsideFolder) {
                    std::string breadcrumb = renderCol.title + " / " + renderCol.folders[currentFolder].name;
                    DrawTextEx(xmbFont, breadcrumb.c_str(), { 420.0f, 210.0f }, 18, 1.0f, Fade(WHITE, 0.6f));
                }

                float startY = 350.0f;
                for (size_t i = 0; i < displayNames.size(); ++i) {
                    float delta = (float)i - rowAnim;
                    float t = 1.0f - ClampF(fabsf(delta), 0.0f, 1.0f);

                    Color itemColor = Fade(WHITE, 0.4f + 0.6f * t);
                    float itemSize = 24.0f + 8.0f * t;
                    float xOffset = 420.0f + 30.0f * t;

                    float yPos = startY + delta * 70.0f;

                    if (renderBrowsingFolders) {
                        const std::string& folderIconPath = renderCol.folders[i].icon;
                        auto folderIconIt = iconCache.find(folderIconPath);
                        if (folderIconIt != iconCache.end() && folderIconIt->second.id != 0) {
                            float rowIconSize = itemSize + 8.0f;
                            float rowIconX = xOffset - rowIconSize - 14.0f;
                            float rowIconY = yPos - (rowIconSize / 2.0f) + (itemSize / 2.0f);
                            DrawTexturePro(folderIconIt->second,
                                (Rectangle){ 0.0f, 0.0f, (float)folderIconIt->second.width, (float)folderIconIt->second.height },
                                (Rectangle){ rowIconX, rowIconY, rowIconSize, rowIconSize },
                                (Vector2){ 0.0f, 0.0f }, 0.0f, Fade(WHITE, 0.4f + 0.6f * t));
                        }
                    }

                    DrawTextEx(xmbFont, displayNames[i].c_str(), { xOffset, yPos }, itemSize, 1.0f, itemColor);
                }
            }

            const char* guideText = "UP/DOWN: Navigate  |  LEFT/RIGHT: Columns  |  ENTER: Open Folder / Run App  |  BACKSPACE: Exit Folder ";
            DrawTextEx(xmbFont, guideText, { 420.0f, 1020.0f }, 16, 1.0f, Fade(WHITE, 0.5f));
        }

        EndDrawing();
    }

    statusThreadRunning = false;
    if (statusThread.joinable()) statusThread.join();

    decodeThreadRunning = false;
    if (ffmpegPipe) {
        pclose(ffmpegPipe);
    }
    if (decodeThread.joinable()) {
        decodeThread.join();
    }
    UnloadTexture(bgVideoTexture);
    for (auto& kv : iconCache) {
        UnloadTexture(kv.second);
    }
    for (auto& kv : statusIcons) {
        UnloadTexture(kv.second);
    }
    UnloadFont(xmbFont);
    UnloadSound(coldboot);
    UnloadSound(sndOk);
    UnloadSound(sndCancel);
    UnloadSound(sndOption);
    CloseAudioDevice();
    CloseWindow();

    return 0;
}