#include "menu.hxx"

#include "cfg.hxx"

#include "imgui.h"

namespace menu {

void style() noexcept {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f;
    s.FrameRounding = 0.0f;
    s.GrabRounding = 0.0f;
    s.ScrollbarRounding = 0.0f;
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 1.0f;
    s.WindowPadding = ImVec2(16.0f, 14.0f);
    s.FramePadding = ImVec2(8.0f, 5.0f);
    s.ItemSpacing = ImVec2(10.0f, 9.0f);
    s.GrabMinSize = 14.0f;
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);

    ImVec4* c = s.Colors;
    const ImVec4 blue(0.16f, 0.62f, 1.0f, 1.0f);
    const ImVec4 deep(0.03f, 0.09f, 0.16f, 0.96f);
    const ImVec4 panel(0.05f, 0.15f, 0.27f, 1.0f);
    const ImVec4 panel_hi(0.08f, 0.26f, 0.46f, 1.0f);
    const ImVec4 text(0.86f, 0.94f, 1.0f, 1.0f);

    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.60f, 0.75f, 1.0f);
    c[ImGuiCol_WindowBg] = deep;
    c[ImGuiCol_PopupBg] = deep;
    c[ImGuiCol_Border] = ImVec4(blue.x, blue.y, blue.z, 0.75f);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = panel;
    c[ImGuiCol_FrameBgHovered] = panel_hi;
    c[ImGuiCol_FrameBgActive] = ImVec4(0.10f, 0.36f, 0.62f, 1.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.20f, 0.38f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.33f, 0.62f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.04f, 0.20f, 0.38f, 0.8f);
    c[ImGuiCol_CheckMark] = blue;
    c[ImGuiCol_SliderGrab] = blue;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.55f, 0.85f, 1.0f, 1.0f);
    c[ImGuiCol_Button] = panel;
    c[ImGuiCol_ButtonHovered] = panel_hi;
    c[ImGuiCol_ButtonActive] = blue;
    c[ImGuiCol_Header] = panel;
    c[ImGuiCol_HeaderHovered] = panel_hi;
    c[ImGuiCol_HeaderActive] = blue;
    c[ImGuiCol_Separator] = ImVec4(blue.x, blue.y, blue.z, 0.45f);
    c[ImGuiCol_ScrollbarBg] = deep;
    c[ImGuiCol_ScrollbarGrab] = panel_hi;
    c[ImGuiCol_ResizeGrip] = ImVec4(blue.x, blue.y, blue.z, 0.3f);
    c[ImGuiCol_ResizeGripHovered] = blue;
}

void draw(int ents, int seen) noexcept {
    cfg::set& s = cfg::cur();
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60.0f, 60.0f), ImGuiCond_FirstUseEver);
    ImGui::PushFont(nullptr, 19.0f);
    if (ImGui::Begin("CYBERLIFE", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextDisabled("model dbh_esp  serial #313 248 317 - 51");
        ImGui::Separator();
        ImGui::Checkbox("Boxes", &s.boxes);
        ImGui::Checkbox("Skeleton", &s.skel);
        ImGui::Checkbox("Names and distance", &s.labels);
        ImGui::Checkbox("Include player", &s.player);
        ImGui::Checkbox("Every bone on the player", &s.player_all_bones);
        ImGui::Separator();
        ImGui::SliderFloat("Box range", &s.box_dist, 5.0f, 300.0f, "%.0f m");
        ImGui::SliderFloat("Label range", &s.label_dist, 5.0f, 300.0f, "%.0f m");
        ImGui::SliderFloat("Skeleton range", &s.skel_dist, 5.0f, 300.0f, "%.0f m");
        ImGui::Separator();
        ImGui::TextDisabled("%d characters drawn, %d in the list", ents, seen);
        ImGui::TextDisabled("insert toggles this window");
    }
    ImGui::End();
    ImGui::PopFont();
}

}
