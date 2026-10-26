#include "nd/scene/ScriptableEntity.h"
#include "nd/core/Profiler.h"

namespace nd {

std::map<std::string, ScriptRegistry::Factory>& ScriptRegistry::Map() {
    static std::map<std::string, Factory> map;
    return map;
}

void ScriptRegistry::Register(const std::string& name, Factory factory) { Map()[name] = std::move(factory); }

Ref<ScriptableEntity> ScriptRegistry::Create(const std::string& name) {
    auto it = Map().find(name);
    if (it == Map().end()) return nullptr;
    return it->second();
}

std::vector<std::string> ScriptRegistry::Names() {
    std::vector<std::string> out;
    for (auto& [k, v] : Map()) out.push_back(k);
    return out;
}

bool ScriptRegistry::Exists(const std::string& name) { return Map().count(name) > 0; }

Entity ScriptableEntity::Instantiate(Entity original, const vec3& position) {
    Entity e = GetScene().DuplicateEntity(original);
    if (e) e.Transform().position = position;
    return e;
}

// ------------------------------------------------------------ field visitors
namespace {
struct LoadVisitor : FieldVisitor {
    const json& j;
    explicit LoadVisitor(const json& js) : j(js) {}
    void Float(const char* n, float& v, float) override { v = JGet(j, n, v); }
    void Int(const char* n, int& v) override { v = JGet(j, n, v); }
    void Bool(const char* n, bool& v) override { v = JGet(j, n, v); }
    void Vec3(const char* n, vec3& v) override { v = JGet(j, n, v); }
    void Color(const char* n, vec4& v) override { v = JGet(j, n, v); }
    void String(const char* n, std::string& v) override { v = JGet(j, n, v); }
    void EntityRef(const char* n, uint64_t& v) override { v = JGet<uint64_t>(j, n, v); }
};
struct SaveVisitor : FieldVisitor {
    json j = json::object();
    void Float(const char* n, float& v, float) override { j[n] = v; }
    void Int(const char* n, int& v) override { j[n] = v; }
    void Bool(const char* n, bool& v) override { j[n] = v; }
    void Vec3(const char* n, vec3& v) override { j[n] = v; }
    void Color(const char* n, vec4& v) override { j[n] = v; }
    void String(const char* n, std::string& v) override { j[n] = v; }
    void EntityRef(const char* n, uint64_t& v) override { j[n] = v; }
};
} // namespace

void LoadScriptFields(ScriptableEntity& script, const json& fields) {
    if (!fields.is_object()) return;
    LoadVisitor v(fields);
    script.Reflect(v);
}

json SaveScriptFields(ScriptableEntity& script) {
    SaveVisitor v;
    script.Reflect(v);
    return v.j;
}

} // namespace nd
