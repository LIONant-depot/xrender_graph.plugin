#ifndef XRENDER_GRAPH_EDITOR_H
#define XRENDER_GRAPH_EDITOR_H
#pragma once

// RenderGraph editor: the generic descriptor editor (inspector, undoable edits, save, compile) over the post chain's nodes and parameters.
// Saving compiles it (the resource pipeline sees the descriptor change); the compiler refuses a chain the renderer could not lower and
// says why in its log. (The Level Editor's Render Graph drawer tab shows what a chain expands to - the passes, sizes, what ran.)
#include "source/Tools/Editor/xeditor_descriptor_editor.h"
#include "plugins/xrender_graph.plugin/source/Module/xrender_graph_descriptor.h"
#include "plugins/xrender_graph.plugin/source/Editor/xrender_graph_canvas.h"

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
            if (!xlionrender::post::Edit(Desc, Edit, Message)) return Message;
            xproperty::settings::context C;
            const auto Before = xproperty::ui::undo::SnapshotToString(*D.getProperties(), &D, C);
            D.FromDesc(Desc);
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
            AddPanel("Post chain", dock::left, [this] { m_DescriptorInspector.Show(); });
            AddPanel("Graph", dock::center, [this]      // (the chain as a graph: xgraph's layout and routes, the material stack's)
            {
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
