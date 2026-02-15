#pragma once

#include "Core/Entity.h"
#include "Util/Log.h"
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <filesystem>

namespace WT {

// ============================================================
// LevelFile — Simple text-based level save/load
// Format: one entity per block, key = value pairs
// ============================================================
class LevelFile {
public:
    // Save scene to a .wtlevel file
    static bool Save(const std::string& path, const Scene& scene) {
        std::ofstream file(path);
        if (!file.is_open()) {
            LOG_ERROR("LevelFile: Failed to open '%s' for writing", path.c_str());
            return false;
        }

        file << "# War Times Level File\n";
        file << "# Version 1\n";
        file << "entity_count = " << scene.GetEntityCount() << "\n\n";

        for (int i = 0; i < scene.GetEntityCount(); i++) {
            const auto& e = scene.GetEntity(i);
            file << "[entity]\n";
            file << "name = " << e.name << "\n";
            file << "mesh_type = " << static_cast<int>(e.meshType) << "\n";
            file << "mesh_name = " << e.meshName << "\n";
            file << "texture_name = " << e.textureName << "\n";
            file << "position = " << e.position[0] << " " << e.position[1] << " " << e.position[2] << "\n";
            file << "rotation = " << e.rotation[0] << " " << e.rotation[1] << " " << e.rotation[2] << "\n";
            file << "scale = " << e.scale[0] << " " << e.scale[1] << " " << e.scale[2] << "\n";
            file << "color = " << e.color[0] << " " << e.color[1] << " " << e.color[2] << " " << e.color[3] << "\n";
            file << "visible = " << (e.visible ? 1 : 0) << "\n";
            file << "cast_shadow = " << (e.castShadow ? 1 : 0) << "\n";
            file << "destructible = " << (e.destructible ? 1 : 0) << "\n";
            file << "health = " << e.maxHealth << "\n";
            file << "debris_count = " << e.debrisCount << "\n";
            file << "debris_scale = " << e.debrisScale << "\n";
            file << "\n";
        }

        file.close();
        LOG_INFO("LevelFile: Saved %d entities to '%s'", scene.GetEntityCount(), path.c_str());
        return true;
    }

    // Load scene from a .wtlevel file (clears existing scene)
    static bool Load(const std::string& path, Scene& scene) {
        std::ifstream file(path);
        if (!file.is_open()) {
            LOG_ERROR("LevelFile: Failed to open '%s' for reading", path.c_str());
            return false;
        }

        scene.Clear();

        std::string line;
        Entity currentEntity;
        bool inEntity = false;

        while (std::getline(file, line)) {
            // Trim whitespace
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                // Empty line — if we were building an entity, commit it
                if (inEntity) {
                    scene.AddEntity(currentEntity.name, currentEntity.meshType);
                    auto& e = scene.GetEntity(scene.GetEntityCount() - 1);
                    e = currentEntity;
                    currentEntity = Entity{};
                    inEntity = false;
                }
                continue;
            }
            line = line.substr(start);

            // Skip comments
            if (line[0] == '#') continue;

            // Entity header
            if (line == "[entity]") {
                if (inEntity) {
                    // Commit previous entity
                    scene.AddEntity(currentEntity.name, currentEntity.meshType);
                    auto& e = scene.GetEntity(scene.GetEntityCount() - 1);
                    e = currentEntity;
                    currentEntity = Entity{};
                }
                inEntity = true;
                continue;
            }

            // Parse key = value
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = TrimStr(line.substr(0, eq));
            std::string val = TrimStr(line.substr(eq + 1));

            if (!inEntity) continue;  // Skip non-entity keys (like entity_count)
            if (key == "entity_count") continue; // metadata, skip

            if (key == "name")            currentEntity.name = val;
            else if (key == "mesh_type")  currentEntity.meshType = static_cast<MeshType>(std::stoi(val));
            else if (key == "mesh_name")  currentEntity.meshName = val;
            else if (key == "texture_name") currentEntity.textureName = val;
            else if (key == "position")   ParseFloat3(val, currentEntity.position);
            else if (key == "rotation")   ParseFloat3(val, currentEntity.rotation);
            else if (key == "scale")      ParseFloat3(val, currentEntity.scale);
            else if (key == "color")      ParseFloat4(val, currentEntity.color);
            else if (key == "visible")    currentEntity.visible = (std::stoi(val) != 0);
            else if (key == "cast_shadow") currentEntity.castShadow = (std::stoi(val) != 0);
            else if (key == "destructible") currentEntity.destructible = (std::stoi(val) != 0);
            else if (key == "health") {
                currentEntity.maxHealth = std::stof(val);
                currentEntity.health = currentEntity.maxHealth;
            }
            else if (key == "debris_count") currentEntity.debrisCount = std::stoi(val);
            else if (key == "debris_scale") currentEntity.debrisScale = std::stof(val);
        }

        // Commit last entity if file didn't end with blank line
        if (inEntity) {
            scene.AddEntity(currentEntity.name, currentEntity.meshType);
            auto& e = scene.GetEntity(scene.GetEntityCount() - 1);
            e = currentEntity;
        }

        file.close();
        LOG_INFO("LevelFile: Loaded %d entities from '%s'", scene.GetEntityCount(), path.c_str());
        return true;
    }

    // List all .wtlevel files in a directory
    static std::vector<std::string> ListLevels(const std::string& directory) {
        std::vector<std::string> result;
        try {
            for (auto& entry : std::filesystem::directory_iterator(directory)) {
                if (entry.path().extension() == ".wtlevel") {
                    result.push_back(entry.path().string());
                }
            }
        } catch (...) {
            // Directory doesn't exist or can't be read
        }
        return result;
    }

    // Extract filename without path/extension
    static std::string GetLevelName(const std::string& path) {
        std::filesystem::path p(path);
        return p.stem().string();
    }

private:
    static std::string TrimStr(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    static void ParseFloat3(const std::string& s, float out[3]) {
        std::istringstream ss(s);
        ss >> out[0] >> out[1] >> out[2];
    }

    static void ParseFloat4(const std::string& s, float out[4]) {
        std::istringstream ss(s);
        ss >> out[0] >> out[1] >> out[2] >> out[3];
    }
};

} // namespace WT
