#ifndef XRENDER_GRAPH_BUNDLE_H
#define XRENDER_GRAPH_BUNDLE_H
#pragma once

// The compiled RenderGraph: a bundle - the root (this text: the graph data the runtime lowers) and the links to what its nodes need,
// compiled shaders and the node library DLL (v3 stages I and J). Today every post node is built into the engine, so [ Links ] is
// empty; the loader accepts a root only when all its links are loaded, so filling the table later does not change its contract.
//      [ Bundle ]      { Version:d  Abi:d }
//      [ Links ]       { Kind:s  Guid:G  Hash:G }        "shader" / "dll"
//      [ PostGraph ]   [ Params ]                         xlionrender::post::Serialize
//      [ Stages ]  [ StagePasses ]  [ StageBindings ]   xlionrender::stage::Serialize (the plugin's Stages/*.stage.txt: the frame stages' descriptions)
#include "dependencies/xLIONRender/src/xlionrender_post_graph_text.h"
#include "dependencies/xLIONRender/src/xlionrender_stage_graph_text.h"

namespace xrender_graph
{
    inline constexpr int bundle_version_v = 7;      // the root's format (2: the PostGraph table's Custom column; 3: ShaderWords; 4: the custom nodes' Params column; 5: the stages; 6: StagesOff; 7: StageParams)
    inline constexpr int node_abi_v       = 1;      // the node contract the root was compiled against (the built-in post nodes)

    struct bundle
    {
        struct link { std::string m_Kind; std::uint64_t m_Guid = 0, m_Hash = 0; };      // shader: m_Guid the node type, m_Hash its SPIR-V's
        int                         m_Version = bundle_version_v, m_Abi = node_abi_v;
        std::vector<link>           m_Links;
        std::vector<std::vector<std::int32_t>> m_Spirv;     // per shader link, in order: the custom nodes' shaders (custom node i: link i)
        xlionrender::post::desc     m_Desc;
        xlionrender::stage::library m_Stages;       // the frame stages by name, in place of the renderer's built-in descriptions
        std::vector<std::string>    m_StagesOff;    // the stages bypassed (xlionrender::stage::bypassable_v)
        std::vector<xlionrender::stage::param> m_StageParams;     // the stages' parameters it sets (frame values that win over the code's)

        // Problem: why the bundle cannot be used (a version or ABI this build does not know, a node row refused)
        xerr Serialize(xtextfile::stream& F, std::string& Problem) noexcept
        {
            if (auto Err = F.Record("Bundle", [&](xerr& E) { false || (E = F.Field("Version", m_Version)) || (E = F.Field("Abi", m_Abi)); }); Err) return Err;
            if (F.isReading() && (m_Version != bundle_version_v || m_Abi != node_abi_v)) { Problem = std::format("the bundle is version {} for node ABI {}; this build reads {} for {}: compile it again", m_Version, m_Abi, bundle_version_v, node_abi_v); return {}; }
            if (auto Err = F.Record("Links"
                , [&](std::size_t& Count, xerr&) { if (F.isReading()) m_Links.resize(Count); else Count = m_Links.size(); }
                , [&](std::size_t i, xerr& E) { false || (E = F.Field("Kind", m_Links[i].m_Kind)) || (E = F.Field("Guid", m_Links[i].m_Guid)) || (E = F.Field("Hash", m_Links[i].m_Hash)); }); Err) return Err;
            // the shaders' words, all links in one table (a row a word: the link it belongs to)
            std::vector<std::pair<int, std::uint32_t>> Words;
            if (!F.isReading()) for (std::size_t l = 0; l < m_Spirv.size(); ++l) for (const auto w : m_Spirv[l]) Words.emplace_back(static_cast<int>(l), static_cast<std::uint32_t>(w));
            if (auto Err = F.Record("ShaderWords"
                , [&](std::size_t& Count, xerr&) { if (F.isReading()) Words.resize(Count); else Count = Words.size(); }
                , [&](std::size_t i, xerr& E) { false || (E = F.Field("Link", Words[i].first)) || (E = F.Field("Word", Words[i].second)); }); Err) return Err;
            if (F.isReading())
            {
                m_Spirv.assign(m_Links.size(), {});
                for (const auto& [l, w] : Words) { if (l < 0 || l >= static_cast<int>(m_Links.size())) { Problem = std::format("a shader word of link {} (there are {})", l, m_Links.size()); return {}; } m_Spirv[std::size_t(l)].push_back(static_cast<std::int32_t>(w)); }
            }
            if (auto Err = xlionrender::post::Serialize(F, m_Desc, Problem); Err || !Problem.empty()) return Err;
            if (auto Err = xlionrender::stage::Serialize(F, m_Stages, Problem); Err || !Problem.empty()) return Err;
            if (auto Err = F.Record("StagesOff"
                , [&](std::size_t& Count, xerr&) { if (F.isReading()) m_StagesOff.resize(Count); else Count = m_StagesOff.size(); }
                , [&](std::size_t i, xerr& E) { E = F.Field("Name", m_StagesOff[i]); }); Err) return Err;
            return F.Record("StageParams"
                , [&](std::size_t& Count, xerr&) { if (F.isReading()) m_StageParams.resize(Count); else Count = m_StageParams.size(); }
                , [&](std::size_t i, xerr& E) { auto& P = m_StageParams[i]; false || (E = F.Field("Name", P.m_Name)) || (E = F.Field("Type", P.m_Type)) || (E = F.Field("Value", P.m_Value)); });
        }
    };
}

#endif
