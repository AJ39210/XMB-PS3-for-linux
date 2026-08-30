#pragma once
#include <string>
#include <vector>

struct XMBItem {
    std::string name;
    std::string exec;
};

struct XMBFolder {
    std::string name;
    std::string icon;
    std::vector<XMBItem> items;
};

struct XMBColumn {
    std::string id;
    std::string title;
    std::string icon;
    std::vector<XMBItem> items;    // For flat items (like the Settings column)
    std::vector<XMBFolder> folders; // For nested folders (like the Apps column)
};

class XMLParser {
public:
    static std::vector<XMBColumn> LoadMenuConfig(const std::string& filepath);
};