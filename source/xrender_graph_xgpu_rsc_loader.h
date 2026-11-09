#ifndef XRENDER_GRAPH_XGPU_RSC_LOADER_H
#define XRENDER_GRAPH_XGPU_RSC_LOADER_H
#pragma once

// The RenderGraph's loader (the core's resource system: xlioncore_resources.cpp compiles it in): the compiled bundle read into its runtime
// form. A missing, old or unreadable bundle loads as nothing (the caller keeps what it had: last known good).
#include "dependencies/xresource_mgr/source/xresource_mgr.h"

namespace xrsc
{
    inline static constexpr auto    render_graph_type_guid_v = xresource::type_guid(xresource::guid_generator::Instance64FromString("RenderGraph"));
    using                           render_graph_ref         = xresource::def_guid<render_graph_type_guid_v>;
}

namespace xrender_graph { struct bundle; }

template<>
struct xresource::loader< xrsc::render_graph_type_guid_v >
{
    constexpr static inline auto         type_name_v        = L"RenderGraph";
    constexpr static inline auto         use_death_march_v  = false;
    using                                data_type          = xrender_graph::bundle;

    static data_type*                    Load          ( xresource::mgr& Mgr,                    const full_guid& GUID );
    static void                          Destroy       ( xresource::mgr& Mgr, data_type&& Data,  const full_guid& GUID );
};

#endif
