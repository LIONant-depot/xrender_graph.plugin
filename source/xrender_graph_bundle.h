#ifndef XRENDER_GRAPH_BUNDLE_H
#define XRENDER_GRAPH_BUNDLE_H
#pragma once

// The compiled RenderGraph: a bundle - the root (this text: the graph data the runtime lowers) and the links to what its nodes need,
// compiled shaders and the node library DLL (v3 stages I and J). Today every post node is built into the engine, so [ Links ] is
// empty; the loader accepts a root only when all its links are loaded, so filling the table later does not change its contract.
//      [ Bundle ]      { Version:d  Abi:d }
//      [ Links ]       { Kind:s  Guid:G  Hash:G }        "shader" / "dll"
//      [ PostGraph ]   [ Params ]                         xlionrender::post::Serialize
#include "dependencies/xLIONRender/src/xlionrender_post_graph_text.h"

namespace xrender_graph
{
    inline constexpr int bundle_version_v = 1;      // the root's format
    inline constexpr int node_abi_v       = 1;      // the node contract the root was compiled against (the built-in post nodes)

    struct bundle
    {
        struct link { std::string m_Kind; std::uint64_t m_Guid = 0, m_Hash = 0; };
        int                         m_Version = bundle_version_v, m_Abi = node_abi_v;
        std::vector<link>           m_Links;
        xlionrender::post::desc     m_Desc;

        // Problem: why the bundle cannot be used (a version or ABI this build does not know, a node row refused)
        xerr Serialize(xtextfile::stream& F, std::string& Problem) noexcept
        {
            if (auto Err = F.Record("Bundle", [&](xerr& E) { false || (E = F.Field("Version", m_Version)) || (E = F.Field("Abi", m_Abi)); }); Err) return Err;
            if (F.isReading() && (m_Version != bundle_version_v || m_Abi != node_abi_v)) { Problem = std::format("the bundle is version {} for node ABI {}; this build reads {} for {}: compile it again", m_Version, m_Abi, bundle_version_v, node_abi_v); return {}; }
            if (auto Err = F.Record("Links"
                , [&](std::size_t& Count, xerr&) { if (F.isReading()) m_Links.resize(Count); else Count = m_Links.size(); }
                , [&](std::size_t i, xerr& E) { false || (E = F.Field("Kind", m_Links[i].m_Kind)) || (E = F.Field("Guid", m_Links[i].m_Guid)) || (E = F.Field("Hash", m_Links[i].m_Hash)); }); Err) return Err;
            return xlionrender::post::Serialize(F, m_Desc, Problem);
        }
    };
}

#endif
