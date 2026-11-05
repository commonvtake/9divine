#pragma once
// Building blocks shared by the demo scene builders (DemoBuilder.cpp, CityBuilder.cpp).
#include <euphoria/Euphoria.h>

namespace demo {

void AddScript(eu::Entity e, const std::string& name, eu::json fields = eu::json::object());

// A cube with a box collider. Textures with generated companions (normal / emissive maps,
// world-space UVs for the city textures) get them set up automatically.
eu::Entity Box(eu::Scene& s, const std::string& name, const eu::vec3& pos, const eu::vec3& size, const eu::vec4& color,
               const std::string& texture = "", float tiling = 1.0f, eu::BodyType body = eu::BodyType::Static,
               const eu::vec3& euler = eu::vec3(0.0f));
// A render-only mesh (no collider).
eu::Entity Mesh(eu::Scene& s, const std::string& name, const std::string& mesh, const eu::vec3& pos, const eu::vec3& scale,
                const eu::vec4& color, eu::Entity parent = {});

struct PlayerRig { eu::Entity player, model, camera; };
PlayerRig AddPlayer(eu::Scene& s, const eu::vec3& pos);
eu::Entity AddSun(eu::Scene& s, const eu::vec3& euler, float intensity);
void AddControlsCard(eu::Scene& s, const std::string& extra);
eu::Entity AddAudio(eu::Scene& s, const std::string& name, const std::string& clip, float volume, bool music, bool spatial,
                    eu::Entity parent = {});
eu::Entity AddPortal(eu::Scene& s, const eu::vec3& pos, const std::string& target, const eu::vec4& color);
eu::Entity AddGameManager(eu::Scene& s, const std::string& title, eu::Entity player, eu::Entity portal);

// City.scene, NoodleBar.scene and Apartment.scene (CityBuilder.cpp)
void BuildCity(const std::string& path);
void BuildNoodleBar(const std::string& path);
void BuildApartment(const std::string& path);

} // namespace demo
