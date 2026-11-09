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
// Right drag pans. The frame stages (the plugin's Stages: the light tiles, the reflections' screen trace and reconstruction) follow, green:
// each pass with what it reads and writes - shown, not edited here.
#include "dependencies/xgraph/source/xgraph_view.h"
#include "plugins/xrender_graph.plugin/source/Module/xrender_graph_descriptor.h"
#include "dependencies/xLIONRender/src/xlionrender_stage_graph_text.h"
#include <format>
#include "dependencies/imgui/imgui.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <string>
#include <vector>

namespace xrender_graph_editor::canvas
{
    namespace post = xlionrender::post;
    namespace gl = xgraph::layout;
    namespace gr = xgraph::routes;

    struct step { std::string m_Name; std::vector<std::string> m_In, m_Out; bool m_bOn = true; bool m_bStage = false; std::string m_Tip, m_Stage; };     // (m_bStage: a frame stage's pass or its source; m_Stage: its stage's name - a checkbox when it can be bypassed)

    struct custom_type { std::uint64_t m_Id = 0; std::string m_Name, m_Description; };      // a system node type with a shader (the plugin's NodeTypes)
    struct state { xgraph::view::view m_View; int m_Menu = -1, m_Selected = -1; std::string m_LastReply; std::vector<custom_type> m_CustomTypes; std::vector<std::string> m_CustomNames; xlionrender::stage::library m_Stages; std::vector<std::string> m_StagesOff;
                   std::string m_CacheKey; std::vector<step> m_CacheSteps; std::vector<gl::edge> m_CacheEdges; std::shared_ptr<gr::result> m_pCacheRoutes; };      // (the layout made when what it shows changes)
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
    inline void Build(const post::desc& D, std::vector<step>& Steps, std::vector<gl::edge>& Edges, const std::vector<std::string>& CustomNames = {}, const xlionrender::stage::library& Stages = {}, const std::vector<std::string>& StagesOff = {}) noexcept
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
        // The frame stages (render graph R3: the plugin's Stages, what the bundle carries): each pass a step, its pins its bindings (a
        // pass's own earlier writes folded), wired from the pass that last wrote that resource; what no stage writes comes from
        // "Frame (code)" - the parts of the frame still hand-written
        if (Stages.empty()) return;
        const int Src = static_cast<int>(Steps.size());
        Steps.push_back({ "Frame (code)", {}, {}, true, true, "What the stages read that the frame's hand-written code makes" });
        std::map<std::string, std::pair<int, int>> Made;
        std::set<std::string> History;                                      // (a history read before this frame wrote it: last frame's)
        for (const auto& [Stage, Sd] : Stages)
            for (const auto& Pa : Sd.m_Passes)
            {
                const bool bOn = std::find(StagesOff.begin(), StagesOff.end(), Stage) == StagesOff.end();
                step St{ Pa.m_Name, {}, {}, bOn, true, std::format("stage {}, {}{}", Stage, !Pa.m_Recorder.empty() ? "recorded by the code (" + Pa.m_Recorder + ") in " + Pa.m_RenderPass : std::format("kernel {}, {} push words, ", Pa.m_Kernel, Pa.m_Push.size()) + (Pa.m_RenderPass.empty() ? "dispatched over " + Pa.m_Size : "drawn in " + Pa.m_RenderPass)
                       , [&] { for (const auto& B : xlionrender::stage::bypassable_v) if (B.m_Stage == Stage) return std::string("\nits checkbox bypasses it: ") + std::string(B.m_Without); return std::string("\n(not bypassable: the frame has no way without it)"); }()), Stage };
                for (const auto* pList : { &Pa.m_Buffers, &Pa.m_Textures, &Pa.m_Images })
                    for (const auto& B : *pList)
                    {
                        if (B.m_DeclareIf == "never") continue;                    // (bound only: the pass does not touch it)
                        if (B.m_Life == xlionrender::rg::lifetime::HISTORY) History.insert(B.m_Name);
                        auto& V = B.m_bWrite ? St.m_Out : St.m_In;
                        if (std::find(V.begin(), V.end(), B.m_Name) == V.end()) V.push_back(B.m_Name);
                        if (B.m_bWrite && (B.m_Policy == xlionrender::rg::policy::PRESERVE || B.m_Policy == xlionrender::rg::policy::PARTIAL) && std::find(St.m_In.begin(), St.m_In.end(), B.m_Name) == St.m_In.end()) St.m_In.push_back(B.m_Name);     // (keeps the contents: reads what was there)
                    }
                const int s = static_cast<int>(Steps.size());
                Steps.push_back(St);
                for (int r = 0; r < static_cast<int>(St.m_In.size()); ++r)
                {
                    const auto& Name = St.m_In[std::size_t(r)];
                    if (const auto It = Made.find(Name); It != Made.end()) { Wire(It->second.first, It->second.second, s, r); continue; }
                    const int From = History.contains(Name) ? 1 : Src;      // (Last frame, else the hand-written code)
                    auto& FO = Steps[std::size_t(From)].m_Out;
                    int Row = static_cast<int>(std::find(FO.begin(), FO.end(), Name) - FO.begin());
                    if (Row == static_cast<int>(FO.size())) FO.push_back(Name);
                    Wire(From, Row, s, r);
                }
                for (int r = 0; r < static_cast<int>(St.m_Out.size()); ++r) Made[St.m_Out[std::size_t(r)]] = { s, r };
            }
    }

    inline void Draw(state& S, const post::desc& D, const edit_fn& EditFn) noexcept
    {
        const auto Send = [&](const std::string& Edit) { S.m_LastReply = Edit + ": " + EditFn(Edit); };
        // The steps, their layout and routes: made again only when what they show changes (every frame they cost the editor most of its
        // frame rate once the stages were in: 205 to 25 fps, 2026-11-10)
        std::string Key;
        for (const auto& N : D.m_Nodes) Key += std::format("{}{}{},", static_cast<int>(N.m_Type), N.m_bOn ? 1 : 0, N.m_Custom);
        for (const auto& n : S.m_CustomNames) Key += n + ";";
        for (const auto& s : S.m_StagesOff) Key += s + "|";
        Key += std::format("#{}#{}", S.m_Stages.size(), ImGui::GetFontSize());
        if (Key != S.m_CacheKey || !S.m_pCacheRoutes)
        {
            S.m_CacheSteps.clear(); S.m_CacheEdges.clear();
            Build(D, S.m_CacheSteps, S.m_CacheEdges, S.m_CustomNames, S.m_Stages, S.m_StagesOff);
            gl::input In(1);
            for (const auto& St : S.m_CacheSteps)
            {
                float W = ImGui::CalcTextSize((St.m_bOn ? St.m_Name : St.m_Name + " (bypassed)").c_str()).x + 16 + 26;     // (+ the bypass checkbox)
                const std::size_t nRows = std::max(St.m_In.size(), St.m_Out.size());
                for (std::size_t r = 0; r < nRows; ++r)
                    W = std::max(W, (r < St.m_In.size() ? ImGui::CalcTextSize(St.m_In[r].c_str()).x : 0.0f) + (r < St.m_Out.size() ? ImGui::CalcTextSize(St.m_Out[r].c_str()).x : 0.0f) + gl::geo::MID_GAP);
                In[0].m_Steps.push_back({ static_cast<std::uint64_t>(In[0].m_Steps.size() + 1), gl::StepW(W), static_cast<int>(std::max<std::size_t>(nRows, 1)) });
            }
            S.m_pCacheRoutes = std::make_shared<gr::result>(gr::Build(In, S.m_CacheEdges));
            S.m_CacheKey = Key;
        }
        const auto& Steps = S.m_CacheSteps;
        const auto& Edges = S.m_CacheEdges;
        const auto& R     = *S.m_pCacheRoutes;
        const int nPost = static_cast<int>(D.m_Nodes.size());      // (steps 2 .. 2 + nPost - 1: the chain's nodes, edited here)

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
        if (bRightClick && Hover > 1 && Hover < 2 + nPost) { S.m_Menu = Hover - 2; ImGui::OpenPopup("##node"); }
        if (Hover >= 2 + nPost && !Steps[std::size_t(Hover)].m_Tip.empty()) ImGui::SetTooltip("%s", Steps[std::size_t(Hover)].m_Tip.c_str());
        const ImU32 kWire = IM_COL32(150, 180, 220, 255), kBox = IM_COL32(48, 52, 60, 255), kHead = IM_COL32(70, 90, 120, 255), kSel = IM_COL32(230, 180, 90, 255), kText = IM_COL32(230, 230, 230, 255), kDim = IM_COL32(140, 140, 140, 255), kStage = IM_COL32(70, 105, 85, 255);     // (kStage: a frame stage's pass)
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
            pDL->AddRectFilled(a, ImVec2(b.x, a.y + V.Len(gl::geo::HEADER_H)), ImGui::GetColorU32(St.m_bStage ? kStage : St.m_bOn ? kHead : kBox), V.Len(4.0f), ImDrawFlags_RoundCornersTop);
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
            if (St.m_bStage && xlionrender::stage::Bypassable(St.m_Stage))      // a stage's pass: its stage on or bypassed (every pass of the stage shows it)
            {
                ImGui::PushID(static_cast<int>(i) + 10000);
                ImGui::SetCursorScreenPos(ImVec2(b.x - 22, a.y + (V.Len(gl::geo::HEADER_H) - ImGui::GetFrameHeight()) * 0.5f));
                bool bOn = St.m_bOn;
                if (ImGui::Checkbox("##stage", &bOn)) Send(std::format("stage {} {}", St.m_Stage, bOn ? 1 : 0));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", St.m_Tip.c_str());
                ImGui::PopID();
            }
            if (i < 2 || static_cast<int>(i) >= 2 + nPost) continue;            // (the Scene, Last frame and the stages: their menu not here)
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
        else ImGui::TextDisabled("Click a node to light its wires; right click it: bypass, remove, move, insert after. Green: the frame stages (the plugin's Stages). Right drag pans, the wheel zooms, Home resets.");
        ImGui::EndChild();
    }
}

#endif
