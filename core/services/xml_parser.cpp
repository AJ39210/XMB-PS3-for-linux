#include "xml_parser.h"
#include "tinyxml2.h"
#include <iostream>

std::vector<XMBColumn> XMLParser::LoadMenuConfig(const std::string& filepath) {
    std::vector<XMBColumn> columns;
    tinyxml2::XMLDocument doc;

    if (doc.LoadFile(filepath.c_str()) != tinyxml2::XML_SUCCESS) {
        std::cerr << "[XMLParser] Failed to load config: " << filepath << std::endl;
        return columns;
    }

    tinyxml2::XMLElement* root = doc.FirstChildElement("XMB");
    if (!root) return columns;

    for (tinyxml2::XMLElement* colElem = root->FirstChildElement("Column"); colElem; colElem = colElem->NextSiblingElement("Column")) {
        XMBColumn col;
        col.id = colElem->Attribute("id") ? colElem->Attribute("id") : "";
        col.title = colElem->Attribute("title") ? colElem->Attribute("title") : "";
        col.icon = colElem->Attribute("icon") ? colElem->Attribute("icon") : "";

        // Parse direct items (e.g. inside Settings column)
        for (tinyxml2::XMLElement* itemElem = colElem->FirstChildElement("Item"); itemElem; itemElem = itemElem->NextSiblingElement("Item")) {
            XMBItem item;
            item.name = itemElem->Attribute("name") ? itemElem->Attribute("name") : "";
            item.exec = itemElem->Attribute("exec") ? itemElem->Attribute("exec") : "";
            col.items.push_back(item);
        }

        // Parse nested folders (e.g. inside Apps column)
        for (tinyxml2::XMLElement* folderElem = colElem->FirstChildElement("Folder"); folderElem; folderElem = folderElem->NextSiblingElement("Folder")) {
            XMBFolder folder;
            folder.name = folderElem->Attribute("name") ? folderElem->Attribute("name") : "";
            folder.icon = folderElem->Attribute("icon") ? folderElem->Attribute("icon") : "";

            for (tinyxml2::XMLElement* itemElem = folderElem->FirstChildElement("Item"); itemElem; itemElem = itemElem->NextSiblingElement("Item")) {
                XMBItem item;
                item.name = itemElem->Attribute("name") ? itemElem->Attribute("name") : "";
                item.exec = itemElem->Attribute("exec") ? itemElem->Attribute("exec") : "";
                folder.items.push_back(item);
            }
            col.folders.push_back(folder);
        }

        columns.push_back(col);
    }

    return columns;
}