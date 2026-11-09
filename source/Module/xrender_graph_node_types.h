#ifndef XRENDER_GRAPH_NODE_TYPES_H
#define XRENDER_GRAPH_NODE_TYPES_H
#pragma once

// The render graph's system node types (stage I): data in this plugin's NodeTypes folder - <Name>.rgn.txt (Id, Name, Category, Code,
// Description) and its compute shader <Code> (xlionrender_post_node.glsl's frame + the function Node). User-made nodes are not here: they
// are RenderGraph sub-graphs (xrender_graph_subgraph.plugin).
#include "dependencies/xtextfile/source/xtextfile.h"
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace xrender_graph::node_types
{
    struct type
    {
        std::uint64_t           m_Id = 0;
        std::string             m_Name, m_Category, m_Code, m_Description;
        std::filesystem::path   m_File;         // the .rgn.txt
        xerr Serialize(xtextfile::stream& F) noexcept
        {
            return F.Record("RenderNodeType", [&](xerr& E)
                { false || (E = F.Field("Id", m_Id)) || (E = F.Field("Name", m_Name)) || (E = F.Field("Category", m_Category)) || (E = F.Field("Code", m_Code)) || (E = F.Field("Description", m_Description)); });
        }
    };

    // Every node type of the folder; Problems: a line a file that does not read
    inline std::vector<type> Read(const std::filesystem::path& Folder, std::string& Problems) noexcept
    {
        std::vector<type> Out;
        std::error_code Ec;
        for (const auto& E : std::filesystem::directory_iterator(Folder, Ec))
        {
            const auto P = E.path();
            if (!P.filename().string().ends_with(".rgn.txt")) continue;
            type T; T.m_File = P;
            xtextfile::stream F;
            if (auto Err = F.Open(true, P.wstring(), xtextfile::file_type::TEXT); Err) { Problems += std::format("{}: does not open\n", P.string()); continue; }
            if (auto Err = T.Serialize(F); Err) { Problems += std::format("{}: {}\n", P.string(), Err.getMessage()); continue; }
            Out.push_back(std::move(T));
        }
        return Out;
    }

    inline bool ReadText(const std::filesystem::path& P, std::string& Out) noexcept
    {
        std::ifstream In(P, std::ios::binary);
        if (!In) return false;
        std::stringstream S; S << In.rdbuf(); Out = S.str();
        return true;
    }
}

#endif
