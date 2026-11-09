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
    if (auto Err = pBundle->Serialize(S, Problem); Err || !Problem.empty()) return nullptr;
    for (std::size_t l = 0; l < pBundle->m_Links.size(); ++l)       // (shaders: their words came with the bundle; a DLL link (stage J) cannot be loaded yet)
        if (pBundle->m_Links[l].m_Kind != "shader" || pBundle->m_Spirv[l].empty()) return nullptr;
    return pBundle.release();
}

void xresource::loader< xrsc::render_graph_type_guid_v >::Destroy( xresource::mgr&, data_type&& Data, const full_guid& )
{
    delete &Data;
}
