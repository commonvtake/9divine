#include "euphoria/core/Xml.h"
#include <cstdlib>
#include <cstring>
#include <cctype>

namespace eu {

const std::string* XmlNode::Attr(const std::string& key) const {
    for (auto& a : attributes) if (a.first == key) return &a.second;
    return nullptr;
}
std::string XmlNode::Str(const std::string& key, const std::string& fallback) const {
    auto* a = Attr(key);
    return a ? *a : fallback;
}
int XmlNode::Int(const std::string& key, int fallback) const {
    auto* a = Attr(key);
    return a && !a->empty() ? (int)std::strtol(a->c_str(), nullptr, 10) : fallback;
}
unsigned XmlNode::UInt(const std::string& key, unsigned fallback) const {
    auto* a = Attr(key);
    return a && !a->empty() ? (unsigned)std::strtoul(a->c_str(), nullptr, 10) : fallback;
}
float XmlNode::Float(const std::string& key, float fallback) const {
    auto* a = Attr(key);
    return a && !a->empty() ? std::strtof(a->c_str(), nullptr) : fallback;
}
bool XmlNode::Bool(const std::string& key, bool fallback) const {
    auto* a = Attr(key);
    if (!a || a->empty()) return fallback;
    return *a == "1" || *a == "true" || *a == "True";
}
const XmlNode* XmlNode::Child(const std::string& childName) const {
    for (auto& c : children) if (c.name == childName) return &c;
    return nullptr;
}
std::vector<const XmlNode*> XmlNode::ChildrenNamed(const std::string& childName) const {
    std::vector<const XmlNode*> out;
    for (auto& c : children) if (c.name == childName) out.push_back(&c);
    return out;
}

namespace {

struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string err;

    explicit Parser(const std::string& text) : s(text) {}

    int Line() const {
        int line = 1;
        for (size_t k = 0; k < i && k < s.size(); k++) if (s[k] == '\n') line++;
        return line;
    }
    bool Fail(const std::string& what) {
        if (err.empty()) err = what + " (line " + std::to_string(Line()) + ")";
        return false;
    }
    bool StartsWith(const char* p) const { return s.compare(i, std::strlen(p), p) == 0; }
    void SkipSpace() { while (i < s.size() && std::isspace((unsigned char)s[i])) i++; }
    bool SkipPast(const char* end) {
        size_t p = s.find(end, i);
        if (p == std::string::npos) return false;
        i = p + std::strlen(end);
        return true;
    }
    static bool NameChar(char c) { return std::isalnum((unsigned char)c) || c == '_' || c == ':' || c == '-' || c == '.' || (unsigned char)c >= 0x80; }
    std::string Name() {
        size_t b = i;
        while (i < s.size() && NameChar(s[i])) i++;
        return s.substr(b, i - b);
    }

    static void AppendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    // Decodes entities in s[b, e).
    static std::string Decode(const std::string& s, size_t b, size_t e) {
        std::string out;
        out.reserve(e - b);
        for (size_t k = b; k < e; k++) {
            if (s[k] != '&') { out += s[k]; continue; }
            size_t semi = s.find(';', k);
            if (semi == std::string::npos || semi > e || semi - k > 10) { out += '&'; continue; }
            std::string ent = s.substr(k + 1, semi - k - 1);
            if (ent == "amp") out += '&';
            else if (ent == "lt") out += '<';
            else if (ent == "gt") out += '>';
            else if (ent == "quot") out += '"';
            else if (ent == "apos") out += '\'';
            else if (!ent.empty() && ent[0] == '#') {
                unsigned cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X') ? (unsigned)std::strtoul(ent.c_str() + 2, nullptr, 16)
                                                                                  : (unsigned)std::strtoul(ent.c_str() + 1, nullptr, 10);
                AppendUtf8(out, cp);
            } else { out += '&'; continue; }
            k = semi;
        }
        return out;
    }

    // Skips the prolog, comments, processing instructions and doctype before/after the root.
    void SkipMisc() {
        for (;;) {
            SkipSpace();
            if (StartsWith("<?")) { if (!SkipPast("?>")) { i = s.size(); return; } }
            else if (StartsWith("<!--")) { if (!SkipPast("-->")) { i = s.size(); return; } }
            else if (StartsWith("<!")) { if (!SkipPast(">")) { i = s.size(); return; } }
            else return;
        }
    }

    bool Element(XmlNode& node, int depth) {
        if (depth > 256) return Fail("XML nested too deeply");
        if (i >= s.size() || s[i] != '<') return Fail("expected '<'");
        i++;
        node.name = Name();
        if (node.name.empty()) return Fail("expected an element name");
        for (;;) {
            SkipSpace();
            if (i >= s.size()) return Fail("unexpected end inside <" + node.name + ">");
            if (s[i] == '/') {
                if (i + 1 < s.size() && s[i + 1] == '>') { i += 2; return true; }
                return Fail("expected '/>'");
            }
            if (s[i] == '>') { i++; break; }
            std::string key = Name();
            if (key.empty()) return Fail("bad attribute in <" + node.name + ">");
            SkipSpace();
            if (i >= s.size() || s[i] != '=') return Fail("expected '=' after " + key);
            i++;
            SkipSpace();
            if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) return Fail("expected a quoted value for " + key);
            char q = s[i++];
            size_t end = s.find(q, i);
            if (end == std::string::npos) return Fail("unterminated attribute " + key);
            node.attributes.emplace_back(key, Decode(s, i, end));
            i = end + 1;
        }
        // Content
        for (;;) {
            if (i >= s.size()) return Fail("missing </" + node.name + ">");
            if (s[i] == '<') {
                if (StartsWith("</")) {
                    i += 2;
                    std::string close = Name();
                    if (close != node.name) return Fail("</" + close + "> closes <" + node.name + ">");
                    SkipSpace();
                    if (i >= s.size() || s[i] != '>') return Fail("expected '>'");
                    i++;
                    return true;
                }
                if (StartsWith("<!--")) { if (!SkipPast("-->")) return Fail("unterminated comment"); continue; }
                if (StartsWith("<![CDATA[")) {
                    size_t b = i + 9, e = s.find("]]>", b);
                    if (e == std::string::npos) return Fail("unterminated CDATA");
                    node.text.append(s, b, e - b);
                    i = e + 3;
                    continue;
                }
                if (StartsWith("<?")) { if (!SkipPast("?>")) return Fail("unterminated processing instruction"); continue; }
                node.children.emplace_back();
                if (!Element(node.children.back(), depth + 1)) return false;
                continue;
            }
            size_t b = i;
            while (i < s.size() && s[i] != '<') i++;
            node.text += Decode(s, b, i);
        }
    }
};

} // namespace

bool ParseXml(const std::string& text, XmlNode& root, std::string* error) {
    Parser p(text);
    // UTF-8 byte order mark
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) p.i = 3;
    p.SkipMisc();
    root = XmlNode();
    if (!p.Element(root, 0)) {
        if (error) *error = p.err;
        return false;
    }
    return true;
}

} // namespace eu
