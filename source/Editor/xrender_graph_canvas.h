#ifndef XRENDER_GRAPH_CANVAS_H
#define XRENDER_GRAPH_CANVAS_H
#pragma once

// The RenderGraph as a graph (render graph stage F): its nodes as steps in columns by dependency, the data between them as wires - laid
// out and routed by xgraph, the same code as the material stack. The data a post node reads and makes:
//      TAA          HDR -> Image                  Bloom     Image -> Bloom
//      Exposure     Image -> Exposure             ToneMap   Image, Bloom, Exposure -> Output
// "Image" is the chain's image: the last node before that makes one (TAA), else the lit HDR. A bypassed node is drawn faded and makes
// nothing: its consumers read what is there without it. Editing: a node's checkbox bypasses it, right click a node for remove, move,
// insert after (only the types that keep the chain valid); every edit is one undoable descriptor edit (EditFn: post::Edit's text, its reply).
// Right drag pans.
#include "dependencies/xgraph/source/xgraph_routes.h"
#include "plugins/xrender_graph.plugin/source/Module/xrender_graph_descriptor.h"
#include "dependencies/imgui/imgui.h"
#include <functional>
#include <string>
#include <vector>

namespace xrender_graph_editor::canvas
{
    namespace post = xlionrender::post;
    namespace gl = xgraph::layout;
    namespace gr = xgraph::routes;

    struct step { std::string m_Name; std::vector<std::string> m_In, m_Out; bool m_bOn = true; };

    struct state { ImVec2 m_Pan{ 0, 0 }; int m_Menu = -1; std::string m_LastReply; };
    using edit_fn = std::function<std::string(const std::string&)>;      // post::Edit's text ("bypass 1 0") -> "ok" or why not

    // The chain's steps (an "HDR" source first: the lit image the chain starts from) and its wires
    inline void Build(const post::desc& D, std::vector<step>& Steps, std::vector<gl::edge>& Edges) noexcept
    {
        Steps.clear(); Edges.clear();
        Steps.push_back({ "Lit image", {}, { "HDR" }, true });
        int Image = 0, ImageRow = 0, Bloom = -1, Expo = -1;          // the producer (step) of each, and its output row
        const auto Wire = [&](int From, int FromRow, int To, int ToRow) { Edges.push_back({ From, FromRow, To, ToRow, (std::uint64_t(From) << 8) | std::uint64_t(FromRow), Edges.size() }); };
        for (const auto& N : D.m_Nodes)
        {
            const int s = static_cast<int>(Steps.size());
            switch (N.m_Type)
            {
                case post::node_type::TAA:      Steps.push_back({ "TAA",      { "HDR" },   { "Image" },    N.m_bOn }); if (N.m_bOn) { Wire(Image, ImageRow, s, 0); Image = s; ImageRow = 0; } break;
                case post::node_type::BLOOM:    Steps.push_back({ "Bloom",    { "Image" }, { "Bloom" },    N.m_bOn }); if (N.m_bOn) { Wire(Image, ImageRow, s, 0); Bloom = s; } break;
                case post::node_type::EXPOSURE: Steps.push_back({ "Exposure", { "Image" }, { "Exposure" }, N.m_bOn }); if (N.m_bOn) { Wire(Image, ImageRow, s, 0); Expo = s; } break;
                case post::node_type::TONE_MAP:
                    Steps.push_back({ "Tone map", { "Image", "Bloom", "Exposure" }, { "Output" }, N.m_bOn });
                    Wire(Image, ImageRow, s, 0);
                    if (Bloom >= 0) Wire(Bloom, 0, s, 1);
                    if (Expo >= 0)  Wire(Expo, 0, s, 2);
                    break;
            }
        }
    }

    inline void Draw(state& S, const post::desc& D, const edit_fn& EditFn) noexcept
    {
        const auto Send = [&](const std::string& Edit) { S.m_LastReply = Edit + ": " + EditFn(Edit); };
        std::vector<step> Steps; std::vector<gl::edge> Edges;
        Build(D, Steps, Edges);
        gl::input In(1);
        for (const auto& St : Steps)
        {
            float W = ImGui::CalcTextSize((St.m_bOn ? St.m_Name : St.m_Name + " (bypassed)").c_str()).x + 16 + 26;     // (+ the bypass checkbox)
            const std::size_t nRows = std::max(St.m_In.size(), St.m_Out.size());
            for (std::size_t r = 0; r < nRows; ++r)
                W = std::max(W, (r < St.m_In.size() ? ImGui::CalcTextSize(St.m_In[r].c_str()).x : 0.0f) + (r < St.m_Out.size() ? ImGui::CalcTextSize(St.m_Out[r].c_str()).x : 0.0f) + gl::geo::MID_GAP);
            In[0].m_Steps.push_back({ static_cast<std::uint64_t>(In[0].m_Steps.size() + 1), gl::StepW(W), static_cast<int>(std::max<std::size_t>(nRows, 1)) });
        }
        const auto R = gr::Build(In, Edges);

        ImGui::BeginChild("##rgcanvas", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) { S.m_Pan.x += ImGui::GetIO().MouseDelta.x; S.m_Pan.y += ImGui::GetIO().MouseDelta.y; }
        auto* pDL = ImGui::GetWindowDrawList();
        const ImVec2 O{ ImGui::GetCursorScreenPos().x + S.m_Pan.x, ImGui::GetCursorScreenPos().y + S.m_Pan.y };
        const ImU32 kWire = IM_COL32(150, 180, 220, 255), kBox = IM_COL32(48, 52, 60, 255), kHead = IM_COL32(70, 90, 120, 255), kText = IM_COL32(230, 230, 230, 255), kDim = IM_COL32(140, 140, 140, 255);
        for (const auto& W : R.m_Wires) pDL->AddLine(ImVec2(O.x + W.m_X0, O.y + W.m_Y0), ImVec2(O.x + W.m_X1, O.y + W.m_Y1), kWire, 2.0f);
        for (const auto& J : R.m_Junctions) pDL->AddCircleFilled(ImVec2(O.x + J.m_X, O.y + J.m_Y), 3.5f, kWire);
        for (std::size_t i = 0; i < Steps.size(); ++i)
        {
            const auto& St = Steps[i]; const auto& B = R.m_Layout.m_Steps[i];
            const ImVec2 a{ O.x + B.m_X, O.y + B.m_Y }, b{ a.x + B.m_W, a.y + B.m_H };
            const float Alpha = St.m_bOn ? 1.0f : 0.4f;
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, Alpha);
            pDL->AddRectFilled(a, b, ImGui::GetColorU32(kBox), 4.0f);
            pDL->AddRectFilled(a, ImVec2(b.x, a.y + gl::geo::HEADER_H), ImGui::GetColorU32(St.m_bOn ? kHead : kBox), 4.0f, ImDrawFlags_RoundCornersTop);
            pDL->AddText(ImVec2(a.x + 8, a.y + 5), ImGui::GetColorU32(kText), St.m_bOn ? St.m_Name.c_str() : (St.m_Name + " (bypassed)").c_str());
            for (std::size_t r = 0; r < St.m_In.size(); ++r)
            {
                const float y = a.y + gl::RowY(static_cast<int>(r));
                pDL->AddCircleFilled(ImVec2(a.x, y), 4.0f, ImGui::GetColorU32(kWire));
                pDL->AddText(ImVec2(a.x + 8, y - 7), ImGui::GetColorU32(kDim), St.m_In[r].c_str());
            }
            for (std::size_t r = 0; r < St.m_Out.size(); ++r)
            {
                const float y = a.y + gl::RowY(static_cast<int>(r));
                pDL->AddCircleFilled(ImVec2(b.x, y), 4.0f, ImGui::GetColorU32(kWire));
                pDL->AddText(ImVec2(b.x - 8 - ImGui::CalcTextSize(St.m_Out[r].c_str()).x, y - 7), ImGui::GetColorU32(kText), St.m_Out[r].c_str());
            }
            ImGui::PopStyleVar();
            if (i == 0) continue;           // (the lit image: not a node)
            const int n = static_cast<int>(i) - 1;
            ImGui::PushID(n);
            // the bypass checkbox in the header's right corner; the header and body: right click for the node's menu
            ImGui::SetCursorScreenPos(ImVec2(b.x - 22, a.y + 4));
            bool bOn = St.m_bOn;
            if (ImGui::Checkbox("##on", &bOn)) Send(std::format("bypass {} {}", n, bOn ? 1 : 0));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(bOn ? "On: uncheck to bypass it (kept in the chain, left out of the frame)" : "Bypassed: check to put it back");
            ImGui::SetCursorScreenPos(a);
            ImGui::InvisibleButton("##body", ImVec2(std::max(1.0f, B.m_W - 26), B.m_H), ImGuiButtonFlags_MouseButtonRight);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { S.m_Menu = n; ImGui::OpenPopup("##node"); }
            if (ImGui::BeginPopup("##node"))
            {
                ImGui::TextDisabled("%s", St.m_Name.c_str());
                if (ImGui::MenuItem(St.m_bOn ? "Bypass" : "Turn on")) Send(std::format("bypass {} {}", n, St.m_bOn ? 0 : 1));
                if (ImGui::MenuItem("Remove")) Send(std::format("remove {}", n));
                if (ImGui::MenuItem("Move earlier", nullptr, false, n > 0)) Send(std::format("move {} {}", n, n - 1));
                if (ImGui::MenuItem("Move later", nullptr, false, n + 1 < static_cast<int>(D.m_Nodes.size()))) Send(std::format("move {} {}", n, n + 2));
                const auto Types = post::Insertable(D, n + 1);
                if (ImGui::BeginMenu("Insert after", !Types.empty()))
                {
                    for (const auto T : Types) if (ImGui::MenuItem(std::string(post::node_names_v[std::size_t(T)]).c_str())) Send(std::format("insert {} {}", n + 1, post::node_names_v[std::size_t(T)]));
                    ImGui::EndMenu();
                }
                if (Types.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Nothing fits after it (each node once; TAA first, the tone map last)");
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + 8, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - 24));
        if (!S.m_LastReply.empty()) ImGui::TextColored(S.m_LastReply.ends_with(": ok") ? ImVec4(0.6f, 0.6f, 0.6f, 1) : ImVec4(1.0f, 0.62f, 0.25f, 1), "%s", S.m_LastReply.c_str());
        else ImGui::TextDisabled("Right click a node: bypass, remove, move, insert after. Its checkbox bypasses it. Right drag pans.");
        ImGui::EndChild();
    }
}

#endif
