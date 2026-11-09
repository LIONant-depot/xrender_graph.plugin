#include "xrender_graph_xgpu_rsc_loader.h"
#include "xrender_graph_bundle.h"

inline static auto s_RenderGraphRegistrations = xresource::common_registrations<xrsc::render_graph_type_guid_v>{};

xresource::loader< xrsc::render_graph_type_guid_v >::data_type* xresource::loader< xrsc::render_graph_type_guid_v >::Load( xresource::mgr& Mgr, const full_guid& GUID )
{
    const std::wstring Path = Mgr.getResourcePath(GUID, type_name_v);
    xtextfile::stream  S;
    if (auto Err = S.Open(true, Path, xtextfile::file_type::TEXT); Err) return nullptr;     // (not compiled yet)
    auto pBundle = std::make_unique<xrender_graph::bundle>();
    std::string Problem;
    if (auto Err = pBundle->Serialize(S, Problem); Err || !Problem.empty() || !pBundle->m_Links.empty()) return nullptr;     // (links: none can be loaded yet - stages I/J)
    return pBundle.release();
}

void xresource::loader< xrsc::render_graph_type_guid_v >::Destroy( xresource::mgr&, data_type&& Data, const full_guid& )
{
    delete &Data;
}
