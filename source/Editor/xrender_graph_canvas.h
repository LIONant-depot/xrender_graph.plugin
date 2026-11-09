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
#include "dependencies/xgraph/source/xgraph_view.h"
#include "plugins/xrender_graph.plugin/source/Module/xrender_graph_descriptor.h"
#include "dependencies/imgui/imgui.h"
#include <functional>
#include <map>
#include <tuple>
#include <string>
#include <vector>

namespace xrender_graph_editor::canvas
{
    namespace post = xlionrender::post;
    namespace gl = xgraph::layout;
    namespace gr = xgraph::routes;

    struct step { std::string m_Name; std::vector<std::string> m_In, m_Out; bool m_bOn = true; };

    struct custom_type { std::uint64_t m_Id = 0; std::string m_Name, m_Description; };      // a system node type with a shader (the plugin's NodeTypes)
    struct state { xgraph::view::view m_View; int m_Menu = -1, m_Selected = -1; std::string m_LastReply; std::vector<custom_type> m_CustomTypes; std::vector<std::string> m_CustomNames; };
    using edit_fn = std::function<std::string(const std::string&)>;      // post::Edit's text ("bypass 1 0") -> "ok" or why not

    // The chain's steps with their real contracts (stage I, step 2): a node's pins are what its passes read and write (post::Lower's
    // declarations, the node's levels and internal images folded: "Bloom" for its levels), its wires from the node that last made that
    // image before it. Two sources: the Scene (the lit image and depth it hands the chain) and Last frame (what a node reads before
    // this frame made it: TAA's history, the bloom's threshold reading last frame's exposure). A bypassed node keeps its pins (faded),
    // makes nothing.
    inline std::string ImageName(const post::image_ref& r) noexcept
    {
        switch (r.m_Image)
        {
            case post::image::HDR:      return "Lit image";
            case post::image::DEPTH:    return "Depth";
            case post::image::TAA_PREV: return "History";
            case post::image::EXPOSURE: return "Exposure";
            case post::image::BLOOM:    return "Bloom";
            case post::image::OUTPUT:   return "Output";
            default:                    return "Image";        // (CHAIN, TAA_CUR, CUSTOM: the chain image)
        }
    }
    inline void Build(const post::desc& D, std::vector<step>& Steps, std::vector<gl::edge>& Edges, const std::vector<std::string>& CustomNames = {}) noexcept
    {
        Steps.clear(); Edges.clear();
        Steps.push_back({ "Scene", {}, { "Lit image", "Depth" }, true });
        Steps.push_back({ "Last frame", {}, {}, true });
        auto All = D; for (auto& N : All.m_Nodes) N.m_bOn = true;          // (the contracts: every node as if on)
        const auto P = post::Lower(All, 1920, 1080);
        std::map<std::string, std::pair<int, int>> Last;                     // image name -> (step, output row) that made it last
        Last["Lit image"] = { 0, 0 }; Last["Depth"] = { 0, 1 };
        const auto Wire = [&](int From, int FromRow, int To, int ToRow) { Edges.push_back({ From, FromRow, To, ToRow, (std::uint64_t(From) << 8) | std::uint64_t(FromRow), Edges.size() }); };
        std::vector<std::tuple<int, int, std::string>> FromLastFrame;        // (step, input row, image)
        for (int n = 0; n < static_cast<int>(D.m_Nodes.size()); ++n)
        {
            const auto& N = D.m_Nodes[std::size_t(n)];
            step S;
            S.m_bOn = N.m_bOn;
            S.m_Name = N.m_Type == post::node_type::TAA ? "TAA" : N.m_Type == post::node_type::BLOOM ? "Bloom" : N.m_Type == post::node_type::EXPOSURE ? "Exposure"
                     : N.m_Type == post::node_type::TONE_MAP ? "Tone map"
                     : (N.m_Custom >= 0 && N.m_Custom < static_cast<int>(CustomNames.size()) && !CustomNames[std::size_t(N.m_Custom)].empty() ? CustomNames[std::size_t(N.m_Custom)] : std::string("Custom"));
            std::vector<std::string> Made;                                  // (what this node's own passes wrote already: an internal read)
            for (const auto& Pa : P.m_Passes)
            {
                if (Pa.m_Node != n) continue;
                for (const auto& A : Pa.m_Accesses)
                {
                    const auto Name = ImageName(A.m_Ref);
                    if (A.m_bWrite) { if (std::find(Made.begin(), Made.end(), Name) == Made.end()) Made.push_back(Name); continue; }
                    if (A.m_Ref.m_Image == post::image::HDR && Pa.m_Kernel == post::kernel::POST && Pa.m_Push == post::push::EXPOSURE) continue;     // (bound, unused: the exposure kernel's second slot)
                    if (std::find(Made.begin(), Made.end(), Name) != Made.end() || std::find(S.m_In.begin(), S.m_In.end(), Name) != S.m_In.end()) continue;
                    S.m_In.push_back(Name);
                }
            }
            S.m_Out = Made;
            const int s = static_cast<int>(Steps.size());
            Steps.push_back(S);
            for (int r = 0; r < static_cast<int>(S.m_In.size()); ++r)
            {
                if (const auto It = Last.find(S.m_In[std::size_t(r)]); It != Last.end()) { if (N.m_bOn) Wire(It->second.first, It->second.second, s, r); }
                else FromLastFrame.emplace_back(s, r, S.m_In[std::size_t(r)]);
            }
            if (N.m_bOn) for (int r = 0; r < static_cast<int>(S.m_Out.size()); ++r) Last[S.m_Out[std::size_t(r)]] = { s, r };
        }
        // Last frame's outputs: one a distinct image read before this frame made it
        for (const auto& [s, r, Name] : FromLastFrame)
        {
            auto& LF = Steps[1].m_Out;
            int Row = static_cast<int>(std::find(LF.begin(), LF.end(), Name) - LF.begin());
            if (Row == static_cast<int>(LF.size())) LF.push_back(Name);
            if (Steps[std::size_t(s)].m_bOn) Wire(1, Row, s, r);
        }
    }

    inline void Draw(state& S, const post::desc& D, const edit_fn& EditFn) noexcept
    {
        const auto Send = [&](const std::string& Edit) { S.m_LastReply = Edit + ": " + EditFn(Edit); };
        std::vector<step> Steps; std::vector<gl::edge> Edges;
        Build(D, Steps, Edges, S.m_CustomNames);
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

        // The frame (xgraph::view, the material stack's): pan, zoom, grid, crisp wires lit for the node under the mouse or selected
        ImGui::BeginChild("##rgcanvas", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        static const xgraph::view::style Style{};
        const bool bRightClick = S.m_View.Input(Style, ImGui::GetContentRegionAvail());
        auto* pDL = ImGui::GetWindowDrawList();
        auto& V = S.m_View;
        V.DrawBackground(pDL, Style);
        const ImVec2 Mouse = ImGui::GetIO().MousePos;
        int Hover = -1;
        for (std::size_t i = 2; i < Steps.size(); ++i)
        {
            const auto& B = R.m_Layout.m_Steps[i];
            const ImVec2 a = V.ToScreen(B.m_X, B.m_Y), b = V.ToScreen(B.m_X + B.m_W, B.m_Y + B.m_H);
            if (ImGui::IsWindowHovered() && Mouse.x >= a.x && Mouse.x < b.x && Mouse.y >= a.y && Mouse.y < b.y) Hover = static_cast<int>(i);
        }
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) S.m_Selected = Hover;      // (left click: select, empty: none)
        if (bRightClick && Hover > 1) { S.m_Menu = Hover - 2; ImGui::OpenPopup("##node"); }
        const ImU32 kWire = IM_COL32(150, 180, 220, 255), kBox = IM_COL32(48, 52, 60, 255), kHead = IM_COL32(70, 90, 120, 255), kSel = IM_COL32(230, 180, 90, 255), kText = IM_COL32(230, 230, 230, 255), kDim = IM_COL32(140, 140, 140, 255);
        {
            std::vector<float> Lit(Edges.size(), 0.0f);
            for (const int Focus : { S.m_Selected, Hover })
                if (Focus > 1) for (std::size_t e = 0; e < Edges.size(); ++e) if (Edges[e].m_From == Focus || Edges[e].m_To == Focus) Lit[e] = Focus == S.m_Selected ? 1.0f : std::max(Lit[e], 0.9f);
            V.DrawRoutes(pDL, Style, R, std::vector<ImU32>(R.m_Sources.size(), kWire), Lit);
        }
        ImFont* pFont = ImGui::GetFont();
        const float Font = ImGui::GetFontSize() * V.m_Zoom;
        const auto Text = [&](ImVec2 At, ImU32 C, const std::string& Sv) { pDL->AddText(pFont, Font, At, C, Sv.c_str()); };
        for (std::size_t i = 0; i < Steps.size(); ++i)
        {
            const auto& St = Steps[i]; const auto& B = R.m_Layout.m_Steps[i];
            const ImVec2 a = V.ToScreen(B.m_X, B.m_Y), b = V.ToScreen(B.m_X + B.m_W, B.m_Y + B.m_H);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, St.m_bOn ? 1.0f : 0.4f);
            pDL->AddRectFilled(a, b, ImGui::GetColorU32(kBox), V.Len(4.0f));
            pDL->AddRectFilled(a, ImVec2(b.x, a.y + V.Len(gl::geo::HEADER_H)), ImGui::GetColorU32(St.m_bOn ? kHead : kBox), V.Len(4.0f), ImDrawFlags_RoundCornersTop);
            if (static_cast<int>(i) == S.m_Selected) pDL->AddRect(a, b, kSel, V.Len(4.0f), 0, 2.0f);
            Text(ImVec2(a.x + V.Len(8), a.y + V.Len(5)), ImGui::GetColorU32(kText), St.m_bOn ? St.m_Name : St.m_Name + " (bypassed)");
            for (std::size_t r = 0; r < St.m_In.size(); ++r)
            {
                const float y = a.y + V.Len(gl::RowY(static_cast<int>(r)));
                pDL->AddCircleFilled(ImVec2(a.x, y), V.Len(4.0f), ImGui::GetColorU32(kWire));
                Text(ImVec2(a.x + V.Len(8), y - Font * 0.5f), ImGui::GetColorU32(kDim), St.m_In[r]);
            }
            for (std::size_t r = 0; r < St.m_Out.size(); ++r)
            {
                const float y = a.y + V.Len(gl::RowY(static_cast<int>(r)));
                pDL->AddCircleFilled(ImVec2(b.x, y), V.Len(4.0f), ImGui::GetColorU32(kWire));
                Text(ImVec2(b.x - V.Len(8) - ImGui::CalcTextSize(St.m_Out[r].c_str()).x * V.m_Zoom, y - Font * 0.5f), ImGui::GetColorU32(kText), St.m_Out[r]);
            }
            ImGui::PopStyleVar();
            if (i < 2) continue;            // (the Scene and Last frame: not nodes)
            const int n = static_cast<int>(i) - 2;
            ImGui::PushID(n);
            ImGui::SetCursorScreenPos(ImVec2(b.x - 22, a.y + (V.Len(gl::geo::HEADER_H) - ImGui::GetFrameHeight()) * 0.5f));      // the bypass checkbox in the header's right corner
            bool bOn = St.m_bOn;
            if (ImGui::Checkbox("##on", &bOn)) Send(std::format("bypass {} {}", n, bOn ? 1 : 0));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(bOn ? "On: uncheck to bypass it (kept in the chain, left out of the frame)" : "Bypassed: check to put it back");
            ImGui::PopID();
        }
        // The node's menu (a right click on it, not a drag)
        if (ImGui::BeginPopup("##node"))
        {
            const int n = S.m_Menu;
            if (n >= 0 && n < static_cast<int>(D.m_Nodes.size()))
            {
                const auto& St = Steps[static_cast<std::size_t>(n) + 2];
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
                // shader nodes (the plugin's NodeTypes): any number after TAA, before the tone map
                if (ImGui::BeginMenu("Insert shader node after", !S.m_CustomTypes.empty()))
                {
                    for (const auto& T : S.m_CustomTypes)
                    {
                        if (ImGui::MenuItem(T.m_Name.c_str())) Send(std::format("insert-custom {} {:016X}", n + 1, T.m_Id));
                        if (ImGui::IsItemHovered() && !T.m_Description.empty()) ImGui::SetTooltip("%s", T.m_Description.c_str());
                    }
                    ImGui::EndMenu();
                }
            }
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + 8, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - 24));
        if (!S.m_LastReply.empty()) ImGui::TextColored(S.m_LastReply.ends_with(": ok") ? ImVec4(0.6f, 0.6f, 0.6f, 1) : ImVec4(1.0f, 0.62f, 0.25f, 1), "%s", S.m_LastReply.c_str());
        else ImGui::TextDisabled("Click a node to light its wires; right click it: bypass, remove, move, insert after. Right drag pans, the wheel zooms, Home resets.");
        ImGui::EndChild();
    }
}

#endif
