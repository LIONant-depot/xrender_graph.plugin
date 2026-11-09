#ifndef XRENDER_GRAPH_EDITOR_H
#define XRENDER_GRAPH_EDITOR_H
#pragma once

// RenderGraph editor: the generic descriptor editor (inspector, undoable edits, save, compile) over the post chain's nodes and parameters.
// Saving compiles it (the resource pipeline sees the descriptor change); the compiler refuses a chain the renderer could not lower and
// says why in its log. (The Level Editor's Render Graph drawer tab shows what a chain expands to - the passes, sizes, what ran.)
#include "source/Tools/Editor/xeditor_descriptor_editor.h"
#include "plugins/xrender_graph.plugin/source/Module/xrender_graph_descriptor.h"
#include "plugins/xrender_graph.plugin/source/Editor/xrender_graph_canvas.h"
#include "plugins/xrender_graph.plugin/source/Module/xrender_graph_node_types.h"

namespace xrender_graph_editor
{
    struct session : xeditor::descriptor_editor
    {
        canvas::state m_Canvas;

        // An edit of the chain (post::Edit's text) as one undoable descriptor edit: the same SnapshotEdit an inspector array edit makes
        // (the console shows it, Ctrl+Z undoes it). A chain the renderer could not lower is refused, nothing changes
        std::string GraphEdit(const std::string& Edit) noexcept
        {
            auto& D = static_cast<xrender_graph::descriptor&>(*m_Document.m_pDescriptor);
            auto Desc = D.ToDesc();
            std::string Message;
            xproperty::settings::context C;
            const auto Before = xproperty::ui::undo::SnapshotToString(*D.getProperties(), &D, C);
            if (Edit.starts_with("insert-custom "))     // "insert-custom <gap> <node type hex>": a shader node (post::Edit knows no node types)
            {
                int Gap = -1; std::uint64_t Type = 0;
                if (std::sscanf(Edit.c_str() + 14, "%d %llx", &Gap, reinterpret_cast<unsigned long long*>(&Type)) != 2 || Gap < 0 || Gap > static_cast<int>(D.m_Nodes.size())) return "not an edit: insert-custom <gap> <node type>";
                auto Cand = D; Cand.m_Nodes.insert(Cand.m_Nodes.begin() + Gap, xrender_graph::node_entry{ xlionrender::post::node_type::CUSTOM, true, Type });
                if (const auto P = xlionrender::post::Lower(Cand.ToDesc(), 64, 64); !P.m_Error.empty()) return "refused: " + P.m_Error;
                D.m_Nodes = Cand.m_Nodes;
            }
            else
            {
                if (!xlionrender::post::Edit(Desc, Edit, Message)) return Message;
                D.FromDesc(Desc);
            }
            const auto After = xproperty::ui::undo::SnapshotToString(*D.getProperties(), &D, C);
            xproperty::ui::undo::ApplySnapshotFromString(*D.getProperties(), &D, Before, C);       // (the command applies it: undo has the before)
            if (auto Err = m_Undo.Execute(std::format("SnapshotEdit -Label {} -Before {} -After {}", xeditor::Quote(Edit), xeditor::Quote(Before), xeditor::Quote(After))); !Err.empty()) return Err;
            return "ok";
        }
        session(xresource::full_guid Guid, xresource_editor::library::guid LibraryGuid, xgpu::device* pDevice) noexcept
            : descriptor_editor("RenderGraph", Guid, LibraryGuid, pDevice)
        {
            m_Document.Load();
            BindDescriptorInspector();
            {   // the system shader node types (the plugin's NodeTypes): the graph's "Insert shader node" menu and the names of the custom nodes
                std::string Problems;
                for (const auto& T : xrender_graph::node_types::Read(std::filesystem::path(xresource_editor::g_LibMgr.m_ProjectPath) / "Cache" / "Plugins" / "xrender_graph.plugin" / "NodeTypes", Problems))
                    m_Canvas.m_CustomTypes.push_back({ T.m_Id, T.m_Name, T.m_Description });
            }
            {   // the frame stages (the plugin's Stages/*.stage.txt, what the compiler puts in the bundle; files in frame order: 1_, 2_ ...): shown in the graph
                std::error_code Ec;
                std::vector<std::filesystem::path> Files;
                for (const auto& E : std::filesystem::directory_iterator(std::filesystem::path(xresource_editor::g_LibMgr.m_ProjectPath) / "Cache" / "Plugins" / "xrender_graph.plugin" / "Stages", Ec))
                    if (E.path().filename().wstring().ends_with(L".stage.txt")) Files.push_back(E.path());
                std::sort(Files.begin(), Files.end());
                for (const auto& File : Files)
                {
                    xlionrender::stage::library L; std::string Problem; xtextfile::stream F;
                    if (!F.Open(true, File.wstring(), xtextfile::file_type::TEXT) && !xlionrender::stage::Serialize(F, L, Problem) && Problem.empty())
                        for (auto& S : L) m_Canvas.m_Stages.push_back(std::move(S));
                }
            }
            AddPanel("Post chain", dock::left, [this] { m_DescriptorInspector.Show(); });
            AddPanel("Graph", dock::center, [this]      // (the chain as a graph: xgraph's layout and routes, the material stack's)
            {
                if (m_Document.isLoaded())
                {
                    m_Canvas.m_CustomNames.clear();         // (per custom node in order: its type's name)
                    for (const auto& N : static_cast<const xrender_graph::descriptor&>(*m_Document.m_pDescriptor).m_Nodes)
                        if (N.m_Type == xlionrender::post::node_type::CUSTOM)
                        {
                            const auto It = std::find_if(m_Canvas.m_CustomTypes.begin(), m_Canvas.m_CustomTypes.end(), [&](const auto& T) { return T.m_Id == N.m_NodeType; });
                            m_Canvas.m_CustomNames.push_back(It != m_Canvas.m_CustomTypes.end() ? It->m_Name : std::format("Custom #{:016X}?", N.m_NodeType));
                        }
                }
                if (m_Document.isLoaded()) canvas::Draw(m_Canvas, static_cast<const xrender_graph::descriptor&>(*m_Document.m_pDescriptor).ToDesc(), [this](const std::string& E) { return GraphEdit(E); });
            });
        }
    };

    inline const xeditor::auto_register_resource_editor g_Registration
    { xrender_graph::type_guid_v
    , [](xresource::full_guid Guid, xresource_editor::library::guid LibraryGuid, xgpu::device* pDevice) -> std::unique_ptr<xeditor::resource_editor>
      { return std::make_unique<session>(Guid, LibraryGuid, pDevice); }
    };
}

#endif // XRENDER_GRAPH_EDITOR_H
