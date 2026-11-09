#ifndef XRENDER_GRAPH_EDITOR_H
#define XRENDER_GRAPH_EDITOR_H
#pragma once

// RenderGraph editor: the generic descriptor editor (inspector, undoable edits, save, compile) over the post chain's nodes and parameters.
// Saving compiles it (the resource pipeline sees the descriptor change); the compiler refuses a chain the renderer could not lower and
// says why in its log. (The Level Editor's Render Graph drawer tab shows what a chain expands to - the passes, sizes, what ran.)
#include "source/Tools/Editor/xeditor_descriptor_editor.h"
#include "plugins/xrender_graph.plugin/source/Module/xrender_graph_descriptor.h"

namespace xrender_graph_editor
{
    struct session : xeditor::descriptor_editor
    {
        session(xresource::full_guid Guid, xresource_editor::library::guid LibraryGuid, xgpu::device* pDevice) noexcept
            : descriptor_editor("RenderGraph", Guid, LibraryGuid, pDevice)
        {
            m_Document.Load();
            BindDescriptorInspector();
            AddPanel("Post chain", dock::left, [this] { m_DescriptorInspector.Show(); });
        }
    };

    inline const xeditor::auto_register_resource_editor g_Registration
    { xrender_graph::type_guid_v
    , [](xresource::full_guid Guid, xresource_editor::library::guid LibraryGuid, xgpu::device* pDevice) -> std::unique_ptr<xeditor::resource_editor>
      { return std::make_unique<session>(Guid, LibraryGuid, pDevice); }
    };
}

#endif // XRENDER_GRAPH_EDITOR_H
