#ifndef XRENDER_GRAPH_DESCRIPTOR_H
#define XRENDER_GRAPH_DESCRIPTOR_H
#pragma once

// The RenderGraph resource: how a Level's frame is finished - today the post chain (xLIONRender's post::desc: the nodes in order, each
// on or bypassed, and the parameters they push with their source and range). The descriptor is what a person edits (the inspector);
// the compiler checks it the way the renderer will (post::Lower) and writes the compiled description (post::Serialize's text) that the
// render module loads into a Level's view.
//
//      <resource>.desc/info.txt         what every resource has
//      <resource>.desc/Descriptor.txt   this descriptor
//
// No UI in here: the compiler and the editor both include this one header.
#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "dependencies/xLIONRender/src/xlionrender_post_graph.h"

namespace xrender_graph
{
    inline constexpr auto type_guid_v = xresource::type_guid(0x1BDCDE0D1FF7D371ull);     // "RenderGraph": must match the TypeGUID of the plugin's resource_pipeline.config.txt
    using render_graph_ref = xresource::def_guid<type_guid_v>;

    namespace post = xlionrender::post;

    inline constexpr auto node_type_v = std::array
    { xproperty::settings::enum_item("TAA",      post::node_type::TAA)
    , xproperty::settings::enum_item("Bloom",    post::node_type::BLOOM)
    , xproperty::settings::enum_item("Exposure", post::node_type::EXPOSURE)
    , xproperty::settings::enum_item("ToneMap",  post::node_type::TONE_MAP)
    };
    inline constexpr auto param_type_v = std::array
    { xproperty::settings::enum_item("Float", post::param_type::FLOAT)
    , xproperty::settings::enum_item("Bool",  post::param_type::BOOL)
    };

    struct node_entry
    {
        post::node_type m_Type = post::node_type::TONE_MAP;
        bool            m_bOn  = true;
        XPROPERTY_DEF
        ( "RenderGraphNode", node_entry
        , obj_member<"Type", &node_entry::m_Type, member_enum_span<node_type_v>, member_help<"What the node does: TAA (temporal antialiasing), Bloom (what is too bright glows), Exposure (adapts to the light), ToneMap (HDR to the display: always last)">>
        , obj_member<"On",   &node_entry::m_bOn,  member_help<"Off: bypassed - kept in the recipe, left out of the frame">>
        )
    };
    XPROPERTY_REG(node_entry)

    struct param_entry
    {
        std::string         m_Name, m_Source;
        post::param_type    m_Type = post::param_type::FLOAT;
        float               m_Default = 0, m_Min = 0, m_Max = 0;
        XPROPERTY_DEF
        ( "RenderGraphParam", param_entry
        , obj_member<"Name",    &param_entry::m_Name,    member_help<"What the nodes call it (BloomThreshold, ...)">>
        , obj_member<"Source",  &param_entry::m_Source,  member_help<"Where a frame reads it: scene.post.<name> (the Level's Environment); empty: the default only">>
        , obj_member<"Type",    &param_entry::m_Type,    member_enum_span<param_type_v>>
        , obj_member<"Default", &param_entry::m_Default, member_help<"Its value with no source">>
        , obj_member<"Min",     &param_entry::m_Min,     member_help<"A value from the source below it is clamped (and reported once)">>
        , obj_member<"Max",     &param_entry::m_Max,     member_help<"A value from the source above it is clamped (and reported once)">>
        )
    };
    XPROPERTY_REG(param_entry)

    struct descriptor : xresource_pipeline::descriptor::base
    {
        descriptor() noexcept { FromDesc(post::desc::Default()); }
        void SetupFromSource(std::string_view) override {}
        void Validate(std::vector<std::string>& Errors) const noexcept override
        {
            if (const auto P = post::Lower(ToDesc(), 64, 64); !P.m_Error.empty()) Errors.push_back(P.m_Error);     // (the renderer's own check: a size of 64 x 64 checks the structure)
        }

        post::desc ToDesc() const noexcept
        {
            post::desc D;
            for (const auto& N : m_Nodes)  D.m_Nodes.push_back({ N.m_Type, N.m_bOn });
            for (const auto& P : m_Params) D.m_Params.push_back({ P.m_Name, P.m_Source, P.m_Type, P.m_Default, P.m_Min, P.m_Max });
            return D;
        }
        void FromDesc(const post::desc& D) noexcept
        {
            m_Nodes.clear(); m_Params.clear();
            for (const auto& N : D.m_Nodes)  m_Nodes.push_back({ N.m_Type, N.m_bOn });
            for (const auto& P : D.m_Params) m_Params.push_back({ P.m_Name, P.m_Source, P.m_Type, P.m_Default, P.m_Min, P.m_Max });
        }

        std::vector<node_entry>  m_Nodes;       // in order: TAA first, the tone map last, one of each
        std::vector<param_entry> m_Params;

        XPROPERTY_VDEF
        ( "RenderGraph", descriptor
        , obj_member<"Nodes",  &descriptor::m_Nodes,  member_ui_open<true>, member_help<"The post chain in order (TAA first, ToneMap last, each once). The Post chain view of the Render Graph drawer tab shows what it expands to">>
        , obj_member<"Params", &descriptor::m_Params, member_help<"The values the nodes push, where a frame reads each and its range">>
        )
    };
    XPROPERTY_VREG(descriptor)

    struct factory final : xresource_pipeline::factory_base
    {
        using xresource_pipeline::factory_base::factory_base;
        std::unique_ptr<xresource_pipeline::descriptor::base> CreateDescriptor() const noexcept override { return std::make_unique<descriptor>(); }
        xresource::type_guid ResourceTypeGUID() const noexcept override { return type_guid_v; }
        const char* ResourceTypeName() const noexcept override { return "RenderGraph"; }
        const xproperty::type::object& ResourceXPropertyObject() const noexcept override { return *xproperty::getObjectByType<descriptor>(); }
    };
    namespace details { struct factory_holder { inline static factory s_Instance{}; }; }
}

#endif // XRENDER_GRAPH_DESCRIPTOR_H
