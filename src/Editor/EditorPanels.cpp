#include "EditorPanels.h"
#include "LevelFile.h"
#include "LevelEditorWindow.h"
#include "Graphics/Renderer.h"
#include "Graphics/Camera.h"
#include "Graphics/FSRUpscaler.h"
#include "Core/ResourceManager.h"
#include "Gameplay/WeaponSystem.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace WT {

// ---- Color constants for our custom look ----
static const ImVec4 kAccent       = ImVec4(0.40f, 0.62f, 1.00f, 1.00f);  // blue accent
static const ImVec4 kAccentDim    = ImVec4(0.30f, 0.48f, 0.78f, 1.00f);
static const ImVec4 kAccentMuted  = ImVec4(0.22f, 0.35f, 0.60f, 0.60f);
static const ImVec4 kTextBright   = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
static const ImVec4 kTextDim      = ImVec4(0.55f, 0.58f, 0.62f, 1.00f);
static const ImVec4 kSectionBg    = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
static const ImVec4 kSectionBar   = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
static const ImVec4 kSectionHover = ImVec4(0.20f, 0.23f, 0.30f, 1.00f);
static const ImVec4 kGreen        = ImVec4(0.30f, 0.82f, 0.48f, 1.00f);
static const ImVec4 kYellow       = ImVec4(1.00f, 0.85f, 0.25f, 1.00f);
static const ImVec4 kRed          = ImVec4(1.00f, 0.35f, 0.35f, 1.00f);
static const ImVec4 kOrange       = ImVec4(1.00f, 0.60f, 0.20f, 1.00f);

// Label width for property rows
static constexpr float kLabelWidth = 120.0f;

void EditorPanels::Init() {
    memset(m_fpsHistory, 0, sizeof(m_fpsHistory));
    memset(m_frameTimeHistory, 0, sizeof(m_frameTimeHistory));
    m_historyIdx = 0;
    AddLog(LogEntry::Info, "War Times Editor initialized");
    AddLog(LogEntry::Info, "F6: toggle editor | F7: level editor | F9: reload models | RMB-drag: camera");
}

void EditorPanels::AddLog(LogEntry::Level level, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    m_logEntries.push_back({ level, std::string(buf) });
    if (m_logEntries.size() > MAX_LOG_ENTRIES)
        m_logEntries.pop_front();
}

// ==================== Main Draw ====================
void EditorPanels::Draw(EditorState& state, Renderer& renderer, Camera& camera,
                         float dt, int fps, float totalTime) {
    m_fpsHistory[m_historyIdx]       = static_cast<float>(fps);
    m_frameTimeHistory[m_historyIdx] = dt * 1000.0f;
    m_historyIdx = (m_historyIdx + 1) % 120;

    DrawMenuBar(state);
    DrawDockspace();
    DrawOutliner(state, renderer, camera, dt, fps, totalTime);
    DrawConsoleDrawer();

    if (showDemoWindow) ImGui::ShowDemoWindow(&showDemoWindow);
}

// ==================== Menu Bar ====================
void EditorPanels::DrawMenuBar(EditorState& state) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));

    if (ImGui::BeginMainMenuBar()) {
        // Engine branding
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        ImGui::Text("WAR TIMES");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit", "Alt+F4")) PostQuitMessage(0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Console",    "~", &m_consoleOpen);
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &showDemoWindow);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Debug")) {
            ImGui::MenuItem("Debug Lines", "F4", &state.showDebug);
            if (ImGui::MenuItem("Wireframe", "F1", &state.wireframe))
                state.rendererDirty = true;
            ImGui::EndMenu();
        }

        // Right side status bar
        float rightW = 360.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - rightW);

        int curFps = (int)m_fpsHistory[(m_historyIdx - 1 + 120) % 120];
        ImVec4 fpsCol = (curFps >= 60) ? kGreen : (curFps >= 30) ? kYellow : kRed;
        ImGui::PushStyleColor(ImGuiCol_Text, fpsCol);
        ImGui::Text("%d FPS", curFps);
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextDisabled("%.1fms", m_frameTimeHistory[(m_historyIdx - 1 + 120) % 120]);

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::TextDisabled("MSAA:%dx", state.msaaSamples);
        ImGui::SameLine();
        ImGui::TextDisabled(state.vsync ? "VSync" : "Uncap");

        ImGui::EndMainMenuBar();
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ==================== Dockspace ====================
void EditorPanels::DrawDockspace() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##Dock", nullptr, flags);
    ImGui::PopStyleVar(3);

    ImGuiID id = ImGui::GetID("WTDock");
    ImGui::DockSpace(id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}

// ==================== Visual Helpers ====================

bool EditorPanels::BeginSection(const char* icon, const char* label, bool defaultOpen) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
    ImGui::PushStyleColor(ImGuiCol_Header,        kSectionBar);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  kSectionHover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,   kSectionHover);

    char buf[128];
    snprintf(buf, sizeof(buf), "%s  %s", icon, label);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;
    if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;

    bool open = ImGui::TreeNodeEx(buf, flags);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    if (open) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 3));
        ImGui::Indent(4.0f);
        ImGui::Spacing();
    }
    return open;
}

void EditorPanels::EndSection() {
    ImGui::Spacing();
    ImGui::Unindent(4.0f);
    ImGui::PopStyleVar(2);
    ImGui::TreePop();
}

void EditorPanels::PropertyLabel(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(-1);
}

void EditorPanels::SectionSeparator() {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.20f, 0.22f, 0.28f, 0.60f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

// ==================== Unified Outliner ====================
void EditorPanels::DrawOutliner(EditorState& state, Renderer& renderer, Camera& camera,
                                 float dt, int fps, float totalTime) {
    (void)totalTime;

    // Set panel initial size & position on first frame
    if (m_firstFrame) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        float panelW = 340.0f;
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - panelW, vp->WorkPos.y));
        ImGui::SetNextWindowSize(ImVec2(panelW, vp->WorkSize.y));
        m_firstFrame = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.09f, 0.11f, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.18f, 0.20f, 0.26f, 0.80f));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("Outliner##main", nullptr, wf);

    // Level Editor toggle
    {
        auto* editor = state.pLevelEditor;
        bool edOpen = editor ? editor->IsOpen() : false;
        if (ImGui::Checkbox("Level Editor Window (F7)", &edOpen)) {
            if (editor) editor->SetOpen(edOpen);
        }
    }

    // Reload Models button
    if (ImGui::Button("Reload Models (F9)")) {
        ResourceManager::Get().ReloadMeshDirectory();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d meshes", ResourceManager::Get().GetMeshCount());
    SectionSeparator();

    SectionPhysics(state);
    SectionNavGrid(state);
    SectionAI(state);
    SectionWeapon(state);
    SectionLevel(state);
    SectionCamera(state, camera);
    SectionCulling(state);
    SectionRenderer(state, renderer);
    SectionPerformance(state, renderer, fps, dt);

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

// ==================== SCENE ====================
void EditorPanels::SectionScene(EditorState& state) {
    if (!BeginSection("\xef\x86\xb6" /*cube icon fallback*/ , "Scene", true)) return;
    // since we can't rely on icon fonts, use plain ASCII markers

    // Object list
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.07f, 0.09f, 1.0f));
    ImGui::BeginChild("##objlist", ImVec2(0, 80), ImGuiChildFlags_Borders);

    struct ObjInfo { const char* icon; const char* name; };
    ObjInfo objects[] = {
        { "[#]", "Rotating Cube" },
        { "[=]", "Ground Plane"  },
        { "[*]", "Debug Visuals" },
    };

    for (int i = 0; i < 3; i++) {
        if (i == 2 && !state.showDebug) continue;

        bool selected = (m_selectedObject == i);
        char buf[64];
        snprintf(buf, sizeof(buf), " %s  %s", objects[i].icon, objects[i].name);

        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.28f, 0.45f, 1.0f));
        if (ImGui::Selectable(buf, selected, ImGuiSelectableFlags_None, ImVec2(0, 20))) {
            m_selectedObject = i;
        }
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Properties for selected object
    if (m_selectedObject >= 0 && m_selectedObject <= 2) {
        SectionSeparator();
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        ImGui::Text("  %s", objects[m_selectedObject].name);
        ImGui::PopStyleColor();
        ImGui::Spacing();

        switch (m_selectedObject) {
        case 0: // Cube
            PropertyLabel("Position");
            ImGui::DragFloat3("##cpos", state.cubePosition, 0.1f);
            PropertyLabel("Scale");
            ImGui::DragFloat3("##cscl", state.cubeScale, 0.01f, 0.01f, 100.0f);
            PropertyLabel("Rot Speed");
            ImGui::DragFloat("##crspd", &state.cubeRotationSpeed, 0.01f, 0.0f, 10.0f);
            PropertyLabel("Color");
            ImGui::ColorEdit4("##ccol", state.cubeColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            {
                float deg = state.cubeRotation * (180.0f / 3.14159f);
                PropertyLabel("Angle");
                ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
                ImGui::Text("%.1f\xc2\xb0", deg);
                ImGui::PopStyleColor();
            }
            break;

        case 1: // Ground
            PropertyLabel("Size");
            ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
            ImGui::Text("400 x 400 units");
            ImGui::PopStyleColor();
            PropertyLabel("Shader");
            ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
            ImGui::Text("Procedural Checker");
            ImGui::PopStyleColor();
            break;

        case 2: // Debug
            PropertyLabel("Visible");
            ImGui::Checkbox("##dbgvis", &state.showDebug);
            break;
        }
    }

    EndSection();
}

// ==================== LEVEL FILE ====================
void EditorPanels::SectionLevel(EditorState& state) {
    if (!BeginSection("LVL", "Level File")) return;

    auto* editor = state.pLevelEditor;
    static char saveAsBuf[128] = {};

    // Save / Load / New buttons
    if (ImGui::Button("New", ImVec2(50, 0))) {
        if (editor) editor->NewLevel(state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(50, 0))) {
        if (editor) {
            // If no current path but saveAsBuf has text, use it
            if (editor->GetCurrentLevelPath().empty() && saveAsBuf[0] != '\0') {
                std::string newPath = editor->GetLevelsDirectory() + std::string(saveAsBuf) + ".wtlevel";
                editor->SetCurrentLevelPath(newPath);
                saveAsBuf[0] = '\0';
            }
            editor->SaveCurrentLevel(state);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load...", ImVec2(60, 0))) {
        ImGui::OpenPopup("##LevelLoadPopup");
    }
    ImGui::SameLine();
    if (ImGui::Button("Hot Swap", ImVec2(75, 0))) {
        state.physicsRebuildRequested = true;
        state.navRebuildRequested = true;
        state.entityDirty = true;
    }

    // Delete button (only if a file is loaded)
    if (editor && !editor->GetCurrentLevelPath().empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Del", ImVec2(38, 0))) {
            ImGui::OpenPopup("##LevelDeleteConfirm");
        }
        if (ImGui::BeginPopup("##LevelDeleteConfirm")) {
            ImGui::Text("Delete '%s'?", LevelFile::GetLevelName(editor->GetCurrentLevelPath()).c_str());
            if (ImGui::Button("Yes, Delete")) {
                editor->DeleteCurrentLevel(state);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // Load popup — list available .wtlevel files
    if (ImGui::BeginPopup("##LevelLoadPopup")) {
        std::string levelsDir;
        if (editor) {
            levelsDir = editor->GetLevelsDirectory();
        }

        auto files = LevelFile::ListLevels(levelsDir);
        if (files.empty()) {
            ImGui::TextDisabled("No .wtlevel files found");
        }
        for (auto& f : files) {
            std::string name = LevelFile::GetLevelName(f);
            if (ImGui::MenuItem(name.c_str())) {
                if (editor) editor->LoadLevel(f, state);
            }
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();

    // Save As — type a name and save as new level file
    if (editor) {
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputText("##saveAsName", saveAsBuf, sizeof(saveAsBuf));
        if (saveAsBuf[0] != '\0') {
            ImGui::SameLine();
            if (ImGui::Button("Save As")) {
                std::string newPath = editor->GetLevelsDirectory() + std::string(saveAsBuf) + ".wtlevel";
                editor->SetCurrentLevelPath(newPath);
                editor->SaveCurrentLevel(state);
                saveAsBuf[0] = '\0';
            }
        } else {
            ImGui::SameLine();
            ImGui::TextDisabled("Save As");
        }
    }

    ImGui::Spacing();

    // Toggle level editor window
    bool edOpen = editor ? editor->IsOpen() : false;
    if (ImGui::Checkbox("Level Editor Window (F7)", &edOpen)) {
        if (editor) editor->SetOpen(edOpen);
    }

    // Info
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    if (editor && !editor->GetCurrentLevelPath().empty())
        ImGui::Text("File: %s", LevelFile::GetLevelName(editor->GetCurrentLevelPath()).c_str());
    else
        ImGui::Text("File: (unsaved)");
    ImGui::Text("Entities: %d", state.scene.GetEntityCount());
    ImGui::PopStyleColor();

    // Status feedback from level operations
    if (editor && editor->GetStatusTimer() > 0.0f) {
        bool isError = editor->GetStatusMessage().find("FAILED") != std::string::npos;
        ImGui::PushStyleColor(ImGuiCol_Text, isError ? ImVec4(1,0.3f,0.3f,1) : ImVec4(0.3f,1,0.3f,1));
        ImGui::Text("%s", editor->GetStatusMessage().c_str());
        ImGui::PopStyleColor();
    }

    EndSection();
}

// ==================== ENTITIES ====================
void EditorPanels::SectionEntities(EditorState& state) {
    if (!BeginSection("ENT", "Entities")) return;

    auto& scene = state.scene;
    int count = scene.GetEntityCount();

    // Spawn controls
    if (ImGui::Button("+ Cube", ImVec2(70, 0))) {
        int idx = scene.AddEntity("", MeshType::Cube);
        state.selectedEntity = idx;
    }
    ImGui::SameLine();

    // Model spawn dropdown
    auto modelNames = ResourceManager::Get().GetModelNames();
    if (!modelNames.empty()) {
        if (ImGui::Button("+ Model", ImVec2(70, 0))) {
            ImGui::OpenPopup("##ModelSpawnPopup");
        }
        if (ImGui::BeginPopup("##ModelSpawnPopup")) {
            for (auto& mname : modelNames) {
                if (ImGui::MenuItem(mname.c_str())) {
                    int idx = scene.AddEntity("", MeshType::Custom);
                    scene.GetEntity(idx).meshName = mname;
                    scene.GetEntity(idx).name = mname + "_" + std::to_string(idx);
                    state.selectedEntity = idx;
                }
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
    }

    // Reload models button (hot-swap from disk)
    // (Moved to main Outliner panel — see DrawOutliner)

    if (count > 0 && state.selectedEntity >= 0) {
        if (ImGui::Button("Dup", ImVec2(40, 0))) {
            int idx = scene.DuplicateEntity(state.selectedEntity);
            if (idx >= 0) state.selectedEntity = idx;
        }
        ImGui::SameLine();
        if (ImGui::Button("Del", ImVec2(40, 0))) {
            scene.RemoveEntity(state.selectedEntity);
            if (state.selectedEntity >= scene.GetEntityCount())
                state.selectedEntity = scene.GetEntityCount() - 1;
        }
    }

    // Refresh count after possible add/dup/delete
    count = scene.GetEntityCount();

    ImGui::Spacing();

    // Entity list
    if (count > 0) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.07f, 0.09f, 1.0f));
        float listHeight = (float)(count > 8 ? 8 : count) * 22.0f + 4.0f;
        ImGui::BeginChild("##entlist", ImVec2(0, listHeight), ImGuiChildFlags_Borders);

        for (int i = 0; i < count; i++) {
            auto& e = scene.GetEntity(i);
            bool selected = (state.selectedEntity == i);

            char buf[128];
            snprintf(buf, sizeof(buf), " [#]  %s", e.name.c_str());

            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.28f, 0.45f, 1.0f));
            if (ImGui::Selectable(buf, selected, ImGuiSelectableFlags_None, ImVec2(0, 20))) {
                state.selectedEntity = i;
            }
            ImGui::PopStyleColor();

            // Visibility toggle on same line
            if (!e.visible) {
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
                ImGui::Text("(hidden)");
                ImGui::PopStyleColor();
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        ImGui::Text("  No entities. Click '+ Cube' or '+ Model'.");
        ImGui::PopStyleColor();
    }

    // Selected entity properties
    if (state.selectedEntity >= 0 && state.selectedEntity < count) {
        SectionSeparator();
        auto& e = scene.GetEntity(state.selectedEntity);

        // Editable name
        char nameBuf[128];
        strncpy_s(nameBuf, e.name.c_str(), sizeof(nameBuf) - 1);
        PropertyLabel("Name");
        if (ImGui::InputText("##entname", nameBuf, sizeof(nameBuf))) {
            e.name = nameBuf;
        }

        PropertyLabel("Type");
        ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        ImGui::Text("%s", MeshTypeName(e.meshType));
        ImGui::PopStyleColor();

        // Model selector for Custom entities
        if (e.meshType == MeshType::Custom) {
            PropertyLabel("Model");
            auto allModels = ResourceManager::Get().GetModelNames();
            int currentModel = -1;
            for (int m = 0; m < (int)allModels.size(); m++) {
                if (allModels[m] == e.meshName) { currentModel = m; break; }
            }
            if (ImGui::BeginCombo("##entmodel", currentModel >= 0 ? allModels[currentModel].c_str() : "<none>")) {
                for (int m = 0; m < (int)allModels.size(); m++) {
                    bool sel = (m == currentModel);
                    if (ImGui::Selectable(allModels[m].c_str(), sel)) {
                        e.meshName = allModels[m];
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        PropertyLabel("Position");
        ImGui::DragFloat3("##entpos", e.position, 0.1f);
        PropertyLabel("Rotation");
        ImGui::DragFloat3("##entrot", e.rotation, 0.5f);
        PropertyLabel("Scale");
        ImGui::DragFloat3("##entscl", e.scale, 0.01f, 0.01f, 100.0f);
        PropertyLabel("Color");
        ImGui::ColorEdit4("##entcol", e.color,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

        SectionSeparator();

        PropertyLabel("Visible");
        ImGui::Checkbox("##entvis", &e.visible);
        ImGui::SameLine();
        PropertyLabel("Shadow");
        ImGui::Checkbox("##entshd", &e.castShadow);

        // ---- Destruction Properties ----
        SectionSeparator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.3f, 1.0f));
        ImGui::Text("  Destruction");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        // Material type combo
        PropertyLabel("Material");
        const char* matNames[] = { "Concrete", "Wood", "Metal", "Glass" };
        int matIdx = static_cast<int>(e.materialType);
        if (ImGui::Combo("##entmat", &matIdx, matNames, IM_ARRAYSIZE(matNames))) {
            e.materialType = static_cast<MaterialType>(matIdx);
        }

        PropertyLabel("Destructible");
        ImGui::Checkbox("##entdest", &e.destructible);

        if (e.destructible) {
            PropertyLabel("Health");
            ImGui::DragFloat("##enthp", &e.health, 1.0f, 0.0f, 10000.0f, "%.0f");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
            ImGui::Text("/ %.0f", e.maxHealth);
            ImGui::PopStyleColor();

            PropertyLabel("Max Health");
            if (ImGui::DragFloat("##entmhp", &e.maxHealth, 1.0f, 1.0f, 10000.0f, "%.0f")) {
                if (e.health > e.maxHealth) e.health = e.maxHealth;
            }

            PropertyLabel("Debris Count");
            ImGui::DragInt("##entdc", &e.debrisCount, 0.1f, 1, 50);
            PropertyLabel("Debris Scale");
            ImGui::DragFloat("##entds", &e.debrisScale, 0.01f, 0.05f, 2.0f, "%.2f");
            PropertyLabel("Break Pieces");
            ImGui::DragInt("##entbp", &e.breakPieceCount, 0.1f, 0, 8);

            // Structural support
            PropertyLabel("Supported By");
            char supBuf[128] = {};
            strncpy(supBuf, e.supportedBy.c_str(), sizeof(supBuf) - 1);
            if (ImGui::InputText("##entsup", supBuf, sizeof(supBuf))) {
                e.supportedBy = supBuf;
            }

            // Voxel chunk destruction
            PropertyLabel("Voxel Destruct");
            ImGui::Checkbox("##entvox", &e.voxelDestruction);
            if (e.voxelDestruction) {
                ImGui::SameLine();
                PropertyLabel("Res");
                if (ImGui::DragInt("##entvoxres", &e.voxelRes, 0.05f, 2, 8)) {
                    e.ResetVoxelMask(); // Reset mask on resolution change
                }
            }

            // Health bar preview
            float frac = e.GetHealthFraction();
            ImVec4 barColor;
            if (frac > 0.5f) barColor = ImVec4((1.0f - frac) * 2.0f, 1.0f, 0.0f, 1.0f);
            else             barColor = ImVec4(1.0f, frac * 2.0f, 0.0f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
            char hpBuf[32];
            snprintf(hpBuf, sizeof(hpBuf), "%.0f / %.0f", e.health, e.maxHealth);
            ImGui::ProgressBar(frac, ImVec2(-1, 16), hpBuf);
            ImGui::PopStyleColor();

            // Damage stage indicator
            const char* stages[] = { "Pristine", "Scratched", "Damaged", "Critical" };
            ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
            ImGui::Text("  Stage: %s", stages[e.GetDamageStage()]);
            ImGui::PopStyleColor();

            // Reset health button
            if (ImGui::Button("Reset HP", ImVec2(80, 0))) {
                e.health = e.maxHealth;
                e.damageFlashTimer = 0.0f;
                e.hitDecalCount = 0;
                e.hitDecalNext = 0;
                if (e.voxelDestruction)
                    e.ResetVoxelMask();
            }
        }
    }

    EndSection();
}

// ==================== LIGHTING ====================
void EditorPanels::SectionLighting(EditorState& state) {
    if (!BeginSection("SUN", "Lighting")) return;

    // Sun
    ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
    ImGui::Text("  Directional Light");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Direction");
    state.lightingDirty |= ImGui::DragFloat3("##sundir", state.sunDirection, 0.01f, -1.0f, 1.0f);
    PropertyLabel("Intensity");
    state.lightingDirty |= ImGui::DragFloat("##sunint", &state.sunIntensity, 0.05f, 0.0f, 10.0f);
    PropertyLabel("Color");
    state.lightingDirty |= ImGui::ColorEdit3("##suncol", state.sunColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

    SectionSeparator();

    // Ambient
    ImGui::PushStyleColor(ImGuiCol_Text, kAccentDim);
    ImGui::Text("  Ambient");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Color");
    state.lightingDirty |= ImGui::ColorEdit3("##ambcol", state.ambientColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    PropertyLabel("Intensity");
    state.lightingDirty |= ImGui::DragFloat("##ambint", &state.ambientIntensity, 0.05f, 0.0f, 5.0f);

    SectionSeparator();

    // Fog
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::Text("  Fog");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Color");
    state.lightingDirty |= ImGui::ColorEdit3("##fogcol", state.fogColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    PropertyLabel("Density");
    state.lightingDirty |= ImGui::DragFloat("##fogden", &state.fogDensity, 0.01f, 0.0f, 5.0f);
    PropertyLabel("Start");
    state.lightingDirty |= ImGui::DragFloat("##fogst", &state.fogStart, 1.0f, 0.0f, 1000.0f);
    PropertyLabel("End");
    state.lightingDirty |= ImGui::DragFloat("##fogen", &state.fogEnd, 1.0f, 0.0f, 2000.0f);

    EndSection();
}

// ==================== SKY ====================
void EditorPanels::SectionSky(EditorState& state) {
    if (!BeginSection("SKY", "Sky / Environment")) return;

    // Sky colors
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 1.0f, 1.0f));
    ImGui::Text("  Atmosphere");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Zenith");
    state.skyDirty |= ImGui::ColorEdit3("##skyzen", state.skyZenithColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    PropertyLabel("Horizon");
    state.skyDirty |= ImGui::ColorEdit3("##skyhor", state.skyHorizonColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    PropertyLabel("Ground");
    state.skyDirty |= ImGui::ColorEdit3("##skygnd", state.skyGroundColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    PropertyLabel("Brightness");
    state.skyDirty |= ImGui::DragFloat("##skybrt", &state.skyBrightness, 0.01f, 0.1f, 5.0f);
    PropertyLabel("Horizon Fall");
    state.skyDirty |= ImGui::DragFloat("##skyhf", &state.skyHorizonFalloff, 0.01f, 0.1f, 3.0f);

    SectionSeparator();

    // Sun disc
    ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
    ImGui::Text("  Sun Disc");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Disc Size");
    // Expose as a more intuitive "size" value: smaller cosAngle = larger disc
    float discAngleDeg = acosf(state.sunDiscSize) * (180.0f / 3.14159f);
    if (ImGui::DragFloat("##sdisc", &discAngleDeg, 0.01f, 0.01f, 5.0f, "%.2f deg")) {
        state.sunDiscSize = cosf(discAngleDeg * (3.14159f / 180.0f));
        state.skyDirty = true;
    }
    PropertyLabel("Glow Int.");
    state.skyDirty |= ImGui::DragFloat("##sglowi", &state.sunGlowIntensity, 0.01f, 0.0f, 2.0f);
    PropertyLabel("Glow Tight");
    state.skyDirty |= ImGui::DragFloat("##sglowf", &state.sunGlowFalloff, 1.0f, 1.0f, 256.0f);

    SectionSeparator();

    // Clouds
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.95f, 1.0f));
    ImGui::Text("  Clouds");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Coverage");
    state.skyDirty |= ImGui::DragFloat("##cldcov", &state.cloudCoverage, 0.01f, 0.0f, 1.0f);
    PropertyLabel("Speed");
    state.skyDirty |= ImGui::DragFloat("##cldspd", &state.cloudSpeed, 0.005f, 0.0f, 0.5f);
    PropertyLabel("Density");
    state.skyDirty |= ImGui::DragFloat("##cldden", &state.cloudDensity, 0.1f, 0.5f, 10.0f);
    PropertyLabel("Height");
    state.skyDirty |= ImGui::DragFloat("##cldhgt", &state.cloudHeight, 0.01f, 0.05f, 1.0f);
    PropertyLabel("Color");
    state.skyDirty |= ImGui::ColorEdit3("##cldcol", state.cloudColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    PropertyLabel("Sun Lit");
    state.skyDirty |= ImGui::DragFloat("##cldsun", &state.cloudSunInfluence, 0.01f, 0.0f, 1.0f);

    EndSection();
}

// ==================== SHADOWS ====================
void EditorPanels::SectionShadows(EditorState& state) {
    if (!BeginSection("SHD", "Shadows")) return;

    PropertyLabel("Enabled");
    ImGui::Checkbox("##shden", &state.shadowsEnabled);

    if (state.shadowsEnabled) {
        PropertyLabel("Intensity");
        ImGui::DragFloat("##shdint", &state.shadowIntensity, 0.01f, 0.0f, 1.0f);
        PropertyLabel("Bias");
        ImGui::DragFloat("##shdbias", &state.shadowBias, 0.0001f, 0.0f, 0.01f, "%.4f");
        PropertyLabel("Normal Bias");
        ImGui::DragFloat("##shdnbias", &state.shadowNormalBias, 0.001f, 0.0f, 0.1f, "%.3f");
        PropertyLabel("Distance");
        ImGui::DragFloat("##shddist", &state.shadowDistance, 0.5f, 5.0f, 100.0f);

        SectionSeparator();

        PropertyLabel("Resolution");
        ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        ImGui::Text("%dx%d", state.shadowMapResolution, state.shadowMapResolution);
        ImGui::PopStyleColor();
    }

    EndSection();
}

// ==================== POST-PROCESSING ====================
void EditorPanels::SectionPostProcess(EditorState& state) {
    if (!BeginSection("FX", "Post Processing")) return;

    // --- Bloom ---
    PropertyLabel("Bloom");
    ImGui::Checkbox("##ppbloom", &state.ppBloomEnabled);

    if (state.ppBloomEnabled) {
        PropertyLabel("  Threshold");
        ImGui::DragFloat("##ppbloomth", &state.ppBloomThreshold, 0.01f, 0.0f, 5.0f);
        PropertyLabel("  Intensity");
        ImGui::DragFloat("##ppbloomint", &state.ppBloomIntensity, 0.01f, 0.0f, 3.0f);
    }

    SectionSeparator();

    // --- Vignette ---
    PropertyLabel("Vignette");
    ImGui::Checkbox("##ppvignette", &state.ppVignetteEnabled);

    if (state.ppVignetteEnabled) {
        PropertyLabel("  Intensity");
        ImGui::DragFloat("##ppvigint", &state.ppVignetteIntensity, 0.01f, 0.0f, 2.0f);
        PropertyLabel("  Smoothness");
        ImGui::DragFloat("##ppvigsm", &state.ppVignetteSmoothness, 0.01f, 0.0f, 2.0f);
    }

    SectionSeparator();

    // --- Color Grading ---
    PropertyLabel("Brightness");
    ImGui::DragFloat("##ppbright", &state.ppBrightness, 0.005f, -1.0f, 1.0f);
    PropertyLabel("Contrast");
    ImGui::DragFloat("##ppcontrast", &state.ppContrast, 0.01f, 0.0f, 2.0f);
    PropertyLabel("Saturation");
    ImGui::DragFloat("##ppsat", &state.ppSaturation, 0.01f, 0.0f, 2.0f);
    PropertyLabel("Gamma");
    ImGui::DragFloat("##ppgamma", &state.ppGamma, 0.01f, 0.5f, 2.0f);
    PropertyLabel("Tint");
    ImGui::ColorEdit3("##pptint", state.ppTint, ImGuiColorEditFlags_Float);

    EndSection();
}

// ==================== ART STYLE ====================
void EditorPanels::SectionArtStyle(EditorState& state) {
    if (!BeginSection("ART", "Art Style")) return;

    // Cel-Shading
    ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
    ImGui::Text("  Cel-Shading");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Enabled");
    state.lightingDirty |= ImGui::Checkbox("##celen", &state.celEnabled);

    if (state.celEnabled) {
        PropertyLabel("Bands");
        state.lightingDirty |= ImGui::DragFloat("##celbands", &state.celBands, 0.1f, 2.0f, 6.0f, "%.0f");
        PropertyLabel("Rim Light");
        state.lightingDirty |= ImGui::DragFloat("##celrim", &state.celRimIntensity, 0.01f, 0.0f, 2.0f);
    }

    SectionSeparator();

    // Ink Outlines
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.8f, 1.0f));
    ImGui::Text("  Ink Outlines");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Enabled");
    ImGui::Checkbox("##outlineen", &state.outlineEnabled);

    if (state.outlineEnabled) {
        PropertyLabel("Thickness");
        ImGui::DragFloat("##outthick", &state.outlineThickness, 0.05f, 0.5f, 4.0f);
        PropertyLabel("Color");
        ImGui::ColorEdit3("##outcol", state.outlineColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    }

    SectionSeparator();

    // Hand-Drawn Effects
    ImGui::PushStyleColor(ImGuiCol_Text, kAccentDim);
    ImGui::Text("  Hand-Drawn");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Paper Grain");
    ImGui::DragFloat("##ppgrain", &state.paperGrainIntensity, 0.002f, 0.0f, 0.15f, "%.3f");
    PropertyLabel("Hatching");
    ImGui::DragFloat("##hatchint", &state.hatchingIntensity, 0.01f, 0.0f, 1.0f);
    if (state.hatchingIntensity > 0.001f) {
        PropertyLabel("  Scale");
        ImGui::DragFloat("##hatchscl", &state.hatchingScale, 0.1f, 1.0f, 16.0f);
    }

    EndSection();
}

// ==================== SSAO ====================
void EditorPanels::SectionSSAO(EditorState& state) {
    if (!BeginSection("AO", "Ambient Occlusion")) return;

    PropertyLabel("Enabled");
    ImGui::Checkbox("##ssaoen", &state.ssaoEnabled);

    if (state.ssaoEnabled) {
        PropertyLabel("Radius");
        ImGui::DragFloat("##ssaorad", &state.ssaoRadius, 0.01f, 0.05f, 5.0f);
        PropertyLabel("Bias");
        ImGui::DragFloat("##ssaobias", &state.ssaoBias, 0.001f, 0.0f, 0.1f, "%.3f");
        PropertyLabel("Intensity");
        ImGui::DragFloat("##ssaoint", &state.ssaoIntensity, 0.05f, 0.0f, 5.0f);

        PropertyLabel("Samples");
        int ks = state.ssaoKernelSize;
        if (ImGui::SliderInt("##ssaokernel", &ks, 4, 64)) {
            state.ssaoKernelSize = ks;
        }
    }

    EndSection();
}

// ==================== CHARACTER ====================
void EditorPanels::SectionCharacter(EditorState& state) {
    if (!BeginSection("CHR", "Character")) return;

    // Mode toggle
    ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
    ImGui::Text("  Mode");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("FPS Mode");
    ImGui::Checkbox("##charmode", &state.characterMode);
    ImGui::SameLine();
    ImGui::TextDisabled("(F8)");

    PropertyLabel("Show Body");
    ImGui::Checkbox("##charbody", &state.charShowBody);

    if (state.characterMode) {
        SectionSeparator();

        // Movement
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
        ImGui::Text("  Movement");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        PropertyLabel("Move Speed");
        ImGui::DragFloat("##charspd", &state.charMoveSpeed, 0.1f, 1.0f, 20.0f);
        PropertyLabel("Sprint Mult");
        ImGui::DragFloat("##charsprint", &state.charSprintMult, 0.1f, 1.0f, 5.0f);
        PropertyLabel("Jump Force");
        ImGui::DragFloat("##charjump", &state.charJumpForce, 0.1f, 1.0f, 20.0f);
        PropertyLabel("Gravity");
        ImGui::DragFloat("##chargrav", &state.charGravity, 0.5f, 1.0f, 50.0f);
        PropertyLabel("Ground Y");
        ImGui::DragFloat("##chargy", &state.charGroundY, 0.1f, -10.0f, 10.0f);
        PropertyLabel("Eye Height");
        ImGui::DragFloat("##chareye", &state.charEyeHeight, 0.05f, 0.5f, 3.0f);

        SectionSeparator();

        // Crouch
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.8f, 0.5f, 1.0f));
        ImGui::Text("  Crouch");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        PropertyLabel("Eye Height");
        ImGui::DragFloat("##croucheye", &state.charCrouchEyeHeight, 0.05f, 0.3f, 1.5f);
        PropertyLabel("Speed Mult");
        ImGui::DragFloat("##crouchspd", &state.charCrouchSpeedMult, 0.05f, 0.1f, 1.0f);
        PropertyLabel("Transition");
        ImGui::DragFloat("##crouchtrans", &state.charCrouchTransSpeed, 0.5f, 1.0f, 20.0f);
        ImGui::TextDisabled("Hold Ctrl to crouch");

        SectionSeparator();

        // Camera Tilt
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
        ImGui::Text("  Camera Tilt");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        PropertyLabel("Enabled");
        ImGui::Checkbox("##tiltena", &state.charCameraTiltEnabled);
        if (state.charCameraTiltEnabled) {
            PropertyLabel("Amount");
            ImGui::DragFloat("##tiltamt", &state.charCameraTiltAmount, 0.1f, 0.5f, 8.0f, "%.1f deg");
            PropertyLabel("Speed");
            ImGui::DragFloat("##tiltspd", &state.charCameraTiltSpeed, 0.5f, 1.0f, 20.0f);
        }

        SectionSeparator();

        // Head Bob
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.8f, 1.0f, 1.0f));
        ImGui::Text("  Head Bob");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        PropertyLabel("Enabled");
        ImGui::Checkbox("##charhb", &state.charHeadBobEnabled);
        if (state.charHeadBobEnabled) {
            PropertyLabel("Speed");
            ImGui::DragFloat("##charhbs", &state.charHeadBobSpeed, 0.5f, 2.0f, 30.0f);
            PropertyLabel("Amount");
            ImGui::DragFloat("##charhba", &state.charHeadBobAmount, 0.005f, 0.0f, 0.2f, "%.3f");
            PropertyLabel("Sway");
            ImGui::DragFloat("##charhbw", &state.charHeadBobSway, 0.005f, 0.0f, 0.1f, "%.3f");
        }

        SectionSeparator();

        // Body Colors
        ImGui::PushStyleColor(ImGuiCol_Text, kAccentDim);
        ImGui::Text("  Body Colors");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        PropertyLabel("Head");
        ImGui::ColorEdit3("##colhead", state.charHeadColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        PropertyLabel("Torso");
        ImGui::ColorEdit3("##coltorso", state.charTorsoColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        PropertyLabel("Arms");
        ImGui::ColorEdit3("##colarms", state.charArmsColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        PropertyLabel("Legs");
        ImGui::ColorEdit3("##collegs", state.charLegsColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

        SectionSeparator();

        // Character Model
        ImGui::PushStyleColor(ImGuiCol_Text, kAccentDim);
        ImGui::Text("  Character Model");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        PropertyLabel("Scale");
        ImGui::DragFloat("##charscale", &state.charModelScale, 0.005f, 0.01f, 1.0f, "%.3f");
    }

    EndSection();
}

// ==================== PHYSICS / COLLISION ====================
void EditorPanels::SectionPhysics(EditorState& state) {
    if (!BeginSection("PHY", "Physics / Collision")) return;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
    ImGui::Text("  Collision");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Enabled");
    ImGui::Checkbox("##physcol", &state.physicsCollisionEnabled);

    PropertyLabel("Show Debug");
    ImGui::Checkbox("##physdebug", &state.physicsShowDebug);

    SectionSeparator();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
    ImGui::Text("  Rebuild");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    if (ImGui::Button("Rebuild Colliders", ImVec2(-1, 0))) {
        state.physicsRebuildRequested = true;
    }
    ImGui::TextDisabled("Rebuilds from scene entities");

    EndSection();
}

// ==================== NAV GRID ====================
void EditorPanels::SectionNavGrid(EditorState& state) {
    if (!BeginSection("NAV", "Navigation Grid")) return;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
    ImGui::Text("  Grid Settings");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Enabled");
    ImGui::Checkbox("##navenabled", &state.navGridEnabled);

    PropertyLabel("Grid Width");
    ImGui::DragInt("##navw", &state.navGridWidth, 1, 4, 200);
    PropertyLabel("Grid Height");
    ImGui::DragInt("##navh", &state.navGridHeight, 1, 4, 200);
    PropertyLabel("Cell Size");
    ImGui::DragFloat("##navcell", &state.navCellSize, 0.1f, 0.25f, 4.0f, "%.2f");
    PropertyLabel("Origin X");
    ImGui::DragFloat("##navox", &state.navOriginX, 0.5f, -100.0f, 100.0f);
    PropertyLabel("Origin Z");
    ImGui::DragFloat("##navoz", &state.navOriginZ, 0.5f, -100.0f, 100.0f);
    PropertyLabel("Grid Y");
    ImGui::DragFloat("##navy", &state.navGridY, 0.1f, -10.0f, 10.0f);

    SectionSeparator();

    PropertyLabel("Show Debug");
    ImGui::Checkbox("##navdebug", &state.navShowDebug);

    if (ImGui::Button("Rebuild from Entities", ImVec2(-1, 0))) {
        state.navRebuildRequested = true;
    }
    ImGui::TextDisabled("Marks cells under entities as blocked");

    EndSection();
}

// ==================== AI AGENTS ====================
void EditorPanels::SectionAI(EditorState& state) {
    if (!BeginSection("AI", "AI Agents")) return;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
    ImGui::Text("  Agent Controls");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Show Debug");
    ImGui::Checkbox("##aidebug", &state.aiShowDebug);

    SectionSeparator();

    // Spawn settings
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.3f, 1.0f));
    ImGui::Text("  Spawn Settings");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Spawn Pos");
    ImGui::DragFloat3("##aispawnpos", state.aiSpawnPos, 0.5f);
    PropertyLabel("Move Speed");
    ImGui::DragFloat("##aidefspd", &state.aiDefaultSpeed, 0.1f, 0.5f, 20.0f);
    PropertyLabel("Chase Speed");
    ImGui::DragFloat("##aichasespd", &state.aiDefaultChaseSpeed, 0.1f, 0.5f, 20.0f);
    PropertyLabel("Detect Range");
    ImGui::DragFloat("##aidetect", &state.aiDefaultDetectRange, 0.5f, 1.0f, 50.0f);
    PropertyLabel("Lose Range");
    ImGui::DragFloat("##ailose", &state.aiDefaultLoseRange, 0.5f, 1.0f, 60.0f);
    PropertyLabel("Color");
    ImGui::ColorEdit3("##aicolor", state.aiDefaultColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

    if (ImGui::Button("Spawn Agent", ImVec2(-1, 0))) {
        state.aiSelectedAgent = -2;  // Signal to Application to spawn
    }

    EndSection();
}

// ==================== WEAPON SYSTEM ====================
void EditorPanels::SectionWeapon(EditorState& state) {
    if (!BeginSection("WPN", "Weapon System")) return;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
    ImGui::Text("  Weapon Settings");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Weapon type selector
    PropertyLabel("Weapon");
    const char* weaponNames[] = { "Rifle", "Pistol", "Shotgun" };
    ImGui::Combo("##wpntype", &state.weaponType, weaponNames, 3);

    PropertyLabel("Show Debug");
    ImGui::Checkbox("##wpndebug", &state.weaponShowDebug);

    PropertyLabel("Show HUD");
    ImGui::Checkbox("##wpnhud", &state.weaponShowHUD);

    SectionSeparator();

    // Gun model settings
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
    ImGui::Text("  Gun Model");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Show current gun model name
    if (state.pWeaponSystem) {
        WeaponType wt = static_cast<WeaponType>(state.weaponType);
        auto& wdef = state.pWeaponSystem->GetWeaponDef(wt);

        // Model selector from loaded models
        auto allModels = ResourceManager::Get().GetModelNames();
        int currentModel = -1;
        for (int m = 0; m < (int)allModels.size(); m++) {
            if (allModels[m] == wdef.gunModelName) { currentModel = m; break; }
        }
        PropertyLabel("Model");
        const char* preview = currentModel >= 0 ? allModels[currentModel].c_str() : "<cubes>";
        if (ImGui::BeginCombo("##wpnmodel", preview)) {
            // Option to use cube-based viewmodel
            if (ImGui::Selectable("<cubes>", wdef.gunModelName.empty())) {
                wdef.gunModelName.clear();
            }
            for (int m = 0; m < (int)allModels.size(); m++) {
                bool sel = (m == currentModel);
                if (ImGui::Selectable(allModels[m].c_str(), sel)) {
                    wdef.gunModelName = allModels[m];
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (!wdef.gunModelName.empty()) {
            PropertyLabel("Scale");
            ImGui::DragFloat("##wpnscale", &wdef.modelScale, 0.01f, 0.01f, 5.0f);
            PropertyLabel("Offset");
            float off[3] = { wdef.modelOffsetX, wdef.modelOffsetY, wdef.modelOffsetZ };
            if (ImGui::DragFloat3("##wpnoff", off, 0.01f)) {
                wdef.modelOffsetX = off[0]; wdef.modelOffsetY = off[1]; wdef.modelOffsetZ = off[2];
            }
            PropertyLabel("Rotation");
            float rot[3] = { wdef.modelRotX, wdef.modelRotY, wdef.modelRotZ };
            if (ImGui::DragFloat3("##wpnrot", rot, 1.0f)) {
                wdef.modelRotX = rot[0]; wdef.modelRotY = rot[1]; wdef.modelRotZ = rot[2];
            }

            SectionSeparator();

            // Gun grip sockets
            ImGui::PushStyleColor(ImGuiCol_Text, kAccentDim);
            ImGui::Text("  Grip Sockets");
            ImGui::PopStyleColor();
            ImGui::Spacing();

            PropertyLabel("R Grip");
            float rg[3] = { wdef.rightGripSocket.x, wdef.rightGripSocket.y, wdef.rightGripSocket.z };
            if (ImGui::DragFloat3("##rgrip", rg, 0.005f, -2.0f, 2.0f, "%.3f")) {
                wdef.rightGripSocket = { rg[0], rg[1], rg[2] };
            }
            PropertyLabel("R Grip Rot");
            float rgr[3] = { wdef.rightGripRotation.x, wdef.rightGripRotation.y, wdef.rightGripRotation.z };
            if (ImGui::DragFloat3("##rgriprot", rgr, 1.0f, -180.0f, 180.0f, "%.1f")) {
                wdef.rightGripRotation = { rgr[0], rgr[1], rgr[2] };
            }
            PropertyLabel("L Grip");
            float lg[3] = { wdef.leftGripSocket.x, wdef.leftGripSocket.y, wdef.leftGripSocket.z };
            if (ImGui::DragFloat3("##lgrip", lg, 0.005f, -2.0f, 2.0f, "%.3f")) {
                wdef.leftGripSocket = { lg[0], lg[1], lg[2] };
            }
            PropertyLabel("L Grip Rot");
            float lgr[3] = { wdef.leftGripRotation.x, wdef.leftGripRotation.y, wdef.leftGripRotation.z };
            if (ImGui::DragFloat3("##lgriprot", lgr, 1.0f, -180.0f, 180.0f, "%.1f")) {
                wdef.leftGripRotation = { lgr[0], lgr[1], lgr[2] };
            }
            PropertyLabel("Elbow Hint");
            float ep[3] = { wdef.elbowPoleOffset.x, wdef.elbowPoleOffset.y, wdef.elbowPoleOffset.z };
            if (ImGui::DragFloat3("##elbowpole", ep, 0.01f, -2.0f, 2.0f, "%.2f")) {
                wdef.elbowPoleOffset = { ep[0], ep[1], ep[2] };
            }
        }
    }

    SectionSeparator();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("  Controls");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::TextWrapped("LMB: Fire | RMB: ADS");
    ImGui::TextWrapped("R: Reload | 1/2/3: Switch");

    EndSection();
}

// ==================== CAMERA ====================
void EditorPanels::SectionCamera(EditorState& state, Camera& camera) {
    if (!BeginSection("CAM", "Camera")) return;

    XMFLOAT3 pos = camera.GetPosition();
    float p[3] = { pos.x, pos.y, pos.z };
    PropertyLabel("Position");
    if (ImGui::DragFloat3("##campos", p, 0.1f))
        camera.SetPosition(p[0], p[1], p[2]);

    XMFLOAT3 fwd = camera.GetForward();
    PropertyLabel("Forward");
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::Text("%.2f  %.2f  %.2f", fwd.x, fwd.y, fwd.z);
    ImGui::PopStyleColor();

    PropertyLabel("Yaw / Pitch");
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::Text("%.1f\xc2\xb0 / %.1f\xc2\xb0",
                camera.GetYaw() * (180.0f / 3.14159f),
                camera.GetPitch() * (180.0f / 3.14159f));
    ImGui::PopStyleColor();

    SectionSeparator();

    PropertyLabel("Move Speed");
    ImGui::DragFloat("##camspd", &state.cameraMoveSpeed, 0.1f, 0.1f, 50.0f);
    PropertyLabel("Sprint Mult");
    ImGui::DragFloat("##camspr", &state.cameraSprintMult, 0.1f, 1.0f, 10.0f);
    PropertyLabel("Sensitivity");
    if (ImGui::DragFloat("##camsen", &state.cameraSensitivity, 0.01f, 0.01f, 1.0f))
        camera.SetSensitivity(state.cameraSensitivity);
    PropertyLabel("FOV");
    if (ImGui::DragFloat("##camfov", &state.cameraFOV, 0.5f, 30.0f, 120.0f))
        state.cameraDirty = true;
    PropertyLabel("Near / Far");
    {
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(w * 0.48f);
        if (ImGui::DragFloat("##camnz", &state.cameraNearZ, 0.01f, 0.001f, 10.0f)) state.cameraDirty = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##camfz", &state.cameraFarZ, 1.0f, 10.0f, 5000.0f)) state.cameraDirty = true;
    }

    EndSection();
}

// ==================== CULLING & STREAMING ====================
void EditorPanels::SectionCulling(EditorState& state) {
    if (!BeginSection("EYE", "Culling")) return;

    PropertyLabel("Frustum Culling");
    ImGui::Checkbox("##cullEnabled", &state.cullingEnabled);

    PropertyLabel("Level Streaming");
    ImGui::Checkbox("##streamEnabled", &state.streamingEnabled);

    if (state.streamingEnabled) {
        PropertyLabel("Stream Distance");
        ImGui::DragFloat("##streamDist", &state.streamDistance, 1.0f, 50.0f, 1000.0f, "%.0f");
    }

    PropertyLabel("Shadow Distance");
    ImGui::DragFloat("##shadowCullDist", &state.shadowCullDistance, 1.0f, 20.0f, 500.0f, "%.0f");

    EndSection();
}

// ==================== RENDERER ====================
void EditorPanels::SectionRenderer(EditorState& state, Renderer& renderer) {
    if (!BeginSection("GPU", "Renderer")) return;

    // GPU info — compact
    const auto& gpu = renderer.GetGPUInfo();
    char narrow[128];
    WideCharToMultiByte(CP_UTF8, 0, gpu.adapterName.c_str(), -1, narrow, sizeof(narrow), nullptr, nullptr);
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::Text("  %s  |  %zu MB VRAM", narrow, gpu.dedicatedVideoMemoryMB);
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Resolution");
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::Text("%d x %d  (%.2f)", renderer.GetWidth(), renderer.GetHeight(), renderer.GetAspectRatio());
    ImGui::PopStyleColor();

    PropertyLabel("VSync");
    if (ImGui::Checkbox("##vsync", &state.vsync)) state.rendererDirty = true;
    PropertyLabel("Wireframe");
    if (ImGui::Checkbox("##wire", &state.wireframe)) state.rendererDirty = true;

    PropertyLabel("MSAA");
    {
        const char* opts[] = { "Off", "2x", "4x", "8x" };
        int idx = 0;
        if (state.msaaSamples == 2) idx = 1;
        else if (state.msaaSamples == 4) idx = 2;
        else if (state.msaaSamples >= 8) idx = 3;
        if (ImGui::Combo("##msaa", &idx, opts, 4)) {
            int vals[] = { 1, 2, 4, 8 };
            state.msaaSamples = vals[idx];
            state.rendererDirty = true;
        }
    }

    PropertyLabel("Clear Color");
    ImGui::ColorEdit4("##clrclr", state.clearColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- FSR Upscaling ----
    PropertyLabel("FSR Upscale");
    if (ImGui::Checkbox("##fsr", &state.fsrEnabled)) state.fsrDirty = true;

    if (state.fsrEnabled) {
        PropertyLabel("FSR Quality");
        {
            const char* opts[] = {
                FSRQualityName(FSRQuality::UltraQuality),
                FSRQualityName(FSRQuality::Quality),
                FSRQualityName(FSRQuality::Balanced),
                FSRQualityName(FSRQuality::Performance)
            };
            if (ImGui::Combo("##fsrq", &state.fsrQuality, opts, 4))
                state.fsrDirty = true;
        }

        PropertyLabel("Sharpness");
        if (ImGui::SliderFloat("##fsrsharp", &state.fsrSharpness, 0.0f, 2.0f, "%.2f"))
            state.fsrDirty = true;

        // Show render resolution info
        float scale = FSRQualityScale(static_cast<FSRQuality>(state.fsrQuality));
        int rw = static_cast<int>(renderer.GetWidth()  * scale);
        int rh = static_cast<int>(renderer.GetHeight() * scale);
        ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        ImGui::Text("  Render: %dx%d -> %dx%d", rw, rh, renderer.GetWidth(), renderer.GetHeight());
        ImGui::PopStyleColor();
    }

    EndSection();
}

// ==================== PERFORMANCE ====================
void EditorPanels::SectionPerformance(EditorState& state, Renderer& renderer, int fps, float dt) {
    if (!BeginSection("SYS", "Performance", false)) return;

    const auto& stats = renderer.GetStats();

    // Stat row
    auto StatRow = [&](const char* label, const char* fmt, ...) {
        ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        ImGui::Text("  %s", label);
        ImGui::PopStyleColor();
        ImGui::SameLine(kLabelWidth);
        va_list args;
        va_start(args, fmt);
        char buf[64];
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        ImGui::TextUnformatted(buf);
    };

    StatRow("FPS",        "%d", fps);
    StatRow("Frame",      "%.2f ms", dt * 1000.0f);
    StatRow("Draw Calls", "%u", stats.drawCalls);
    StatRow("Triangles",  "%u", stats.triangles);

    // Culling stats
    if (state.cullingEnabled) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        StatRow("Entities",       "%d", state.cullStatsTotal);
        StatRow("Rendered",       "%d", state.cullStatsRendered);
        StatRow("Frustum Culled", "%d", state.cullStatsFrustum);
        StatRow("Stream Culled",  "%d", state.cullStatsDistance);
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_PlotLines, kAccent);
    ImGui::PlotLines("##fps", m_fpsHistory, 120, m_historyIdx, "FPS", 0.0f, 200.0f, ImVec2(-1, 40));
    ImGui::PlotLines("##ft",  m_frameTimeHistory, 120, m_historyIdx, "ms", 0.0f, 50.0f, ImVec2(-1, 40));
    ImGui::PopStyleColor();

    EndSection();
}

// ==================== CONSOLE DRAWER ====================
void EditorPanels::DrawConsoleDrawer() {
    if (!m_consoleOpen) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float consoleW = vp->WorkSize.x;
    float consoleY = vp->WorkPos.y + vp->WorkSize.y - m_consoleHeight;

    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, consoleY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(consoleW, m_consoleHeight), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.08f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.18f, 0.20f, 0.26f, 0.80f));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                          ImGuiWindowFlags_NoDocking;

    if (ImGui::Begin("##Console", &m_consoleOpen, wf)) {
        // Header bar
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        ImGui::Text("CONSOLE");
        ImGui::PopStyleColor();
        ImGui::SameLine();

        // Filter toggles (compact)
        static bool fInfo = true, fWarn = true, fErr = true;
        ImGui::SameLine(100);
        ImGui::PushStyleColor(ImGuiCol_Text, kTextBright);  ImGui::Checkbox("I##filt", &fInfo); ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kYellow);      ImGui::Checkbox("W##filt", &fWarn); ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kRed);          ImGui::Checkbox("E##filt", &fErr);  ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) m_logEntries.clear();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        ImGui::Text("(%d)", (int)m_logEntries.size());
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.20f, 0.22f, 0.28f, 0.50f));
        ImGui::Separator();
        ImGui::PopStyleColor();

        // Log region
        float footerH = ImGui::GetFrameHeightWithSpacing() + 4;
        if (ImGui::BeginChild("##logscr", ImVec2(0, -footerH), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));

            for (const auto& e : m_logEntries) {
                if (e.level == LogEntry::Info  && !fInfo) continue;
                if (e.level == LogEntry::Warn  && !fWarn) continue;
                if (e.level == LogEntry::Error && !fErr)  continue;

                ImVec4 col;
                const char* pfx;
                switch (e.level) {
                    case LogEntry::Warn:  col = kYellow; pfx = "! "; break;
                    case LogEntry::Error: col = kRed;    pfx = "X "; break;
                    default:              col = ImVec4(0.65f, 0.67f, 0.70f, 1.0f); pfx = "  "; break;
                }
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted((std::string(pfx) + e.text).c_str());
                ImGui::PopStyleColor();
            }

            if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();

        // Input
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
        ImGui::PushItemWidth(-1);
        if (ImGui::InputText("##cin", m_consoleInput, sizeof(m_consoleInput), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (m_consoleInput[0]) {
                AddLog(LogEntry::Info, "> %s", m_consoleInput);
                std::string cmd(m_consoleInput);
                if (cmd == "help")        AddLog(LogEntry::Info, "Commands: help, clear, quit");
                else if (cmd == "clear")  m_logEntries.clear();
                else if (cmd == "quit" || cmd == "exit") PostQuitMessage(0);
                else                      AddLog(LogEntry::Warn, "Unknown: %s", m_consoleInput);
                m_consoleInput[0] = 0;
            }
            ImGui::SetKeyboardFocusHere(-1);
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleColor();
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

} // namespace WT
