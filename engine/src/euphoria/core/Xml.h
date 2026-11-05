#pragma once
// A small XML reader: enough for Tiled maps (.tmx/.tsx) and similar tool formats.
// Elements, attributes, text, comments, CDATA, the five standard entities and numeric
// character references. No DTDs, no namespaces (prefixes are kept as part of names).
#include <string>
#include <vector>
#include <memory>

namespace eu {

struct XmlNode {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<XmlNode> children;
    std::string text;                       // concatenated character data directly inside

    const std::string* Attr(const std::string& key) const;
    std::string Str(const std::string& key, const std::string& fallback = "") const;
    int Int(const std::string& key, int fallback = 0) const;
    unsigned UInt(const std::string& key, unsigned fallback = 0) const;
    float Float(const std::string& key, float fallback = 0.0f) const;
    bool Bool(const std::string& key, bool fallback = false) const;   // "1"/"true"

    const XmlNode* Child(const std::string& childName) const;          // first with that name
    std::vector<const XmlNode*> ChildrenNamed(const std::string& childName) const;
};

// Parses a document and returns its root element. On malformed input returns false and
// describes the problem (with a line number) in `error`.
bool ParseXml(const std::string& text, XmlNode& root, std::string* error = nullptr);

} // namespace eu
