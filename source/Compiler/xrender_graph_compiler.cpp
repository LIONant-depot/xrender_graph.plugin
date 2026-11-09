// The compiler of the RenderGraph resource: its descriptor (the post chain's nodes and parameters) in, checked the way the renderer will
// lower it (post::Lower: one tone map and last, TAA first, each node once, every node's parameters declared, their sources known), out
// the compiled description - post::Serialize's text, what the render module loads into a Level's view. An invalid chain is an error
// naming the node or parameter (the renderer would otherwise tone map only).
#include "dependencies/xstrtool/source/xstrtool.h"
#include "plugins/xrender_graph.plugin/source/Module/xrender_graph_descriptor.h"
#include "plugins/xrender_graph.plugin/source/xrender_graph_bundle.h"
#include "source/Compiler/xrender_graph_compiler.h"
#include "plugins/xrender_graph.plugin/source/Module/xrender_graph_node_types.h"
#include <shaderc/shaderc.hpp>
#if defined(_WIN32)
    #pragma comment(lib, "../../dependencies/shaderc/lib/shaderc_combined.lib")
#endif

#include "dependencies/xproperty/source/xcore/my_properties.cpp"

namespace xrender_graph_compiler
{
    struct implementation final : xrender_graph_compiler::instance
    {
        xerr onCompile(void) override
        {
            displayProgressBar("Reading the descriptor", 0.0f);
            const auto DescriptorFileName = std::format(L"{}/{}/Descriptor.txt", m_ProjectPaths.m_Project, m_InputSrcDescriptorPath);
            auto BaseDescriptor = xresource_pipeline::factory_base::Find(std::string_view{ "RenderGraph" })->CreateDescriptor();
            xproperty::settings::context Context = {};
            if (std::filesystem::exists(DescriptorFileName))        // (a new resource has none until it is saved: the default chain)
                if (auto Err = BaseDescriptor->Serialize(true, DescriptorFileName, Context); Err) return Err;
            auto& Descriptor = *static_cast<xrender_graph::descriptor*>(BaseDescriptor.get());

            displayProgressBar("Checking the chain", 0.3f);
            std::vector<std::string> Errors;
            Descriptor.Validate(Errors);
            if (!Errors.empty())
            {
                for (const auto& E : Errors) LogMessage(xresource_pipeline::msg_type::ERROR, E);
                return xerr::create_f<state, "RenderGraph: the chain is not valid (see the log)">();
            }

            displayProgressBar("Writing the compiled chain", 0.7f);
            xrender_graph::bundle B;
            B.m_Desc = Descriptor.ToDesc();
            for (const auto& S : Descriptor.m_Stages) if (!S.m_bOn) B.m_StagesOff.push_back(S.m_Name);
            for (const auto& P : Descriptor.m_StageParams) if (const auto* pP = xlionrender::stage::Parameter(P.m_Name)) B.m_StageParams.push_back({ P.m_Name, std::string(pP->m_Type), P.m_Value });

            // The custom nodes' shaders (stage I): each its system node type's GLSL (the plugin's NodeTypes), after the node frame
            // (xlionrender_post_node.glsl), compiled to SPIR-V - a shader link of the bundle, in the order of the custom nodes
            if (std::any_of(Descriptor.m_Nodes.begin(), Descriptor.m_Nodes.end(), [](const auto& N) { return N.m_Type == xlionrender::post::node_type::CUSTOM; }))
            {
                displayProgressBar("Compiling the node shaders", 0.5f);
                const std::filesystem::path Project = m_ProjectPaths.m_Project;
                std::string Problems, Frame;
                const auto Types = xrender_graph::node_types::Read(Project / "Cache" / "Plugins" / "xrender_graph.plugin" / "NodeTypes", Problems);
                if (!Problems.empty()) LogMessage(xresource_pipeline::msg_type::WARNING, Problems);
                if (!xrender_graph::node_types::ReadText(Project / "Cache" / "dependencies" / "xLIONRender" / "src" / "shaders" / "xlionrender_post_node.glsl", Frame))
                    return xerr::create_f<state, "RenderGraph: the node frame (xlionrender_post_node.glsl) is missing">();
                shaderc::Compiler Compiler;
                for (std::size_t n = 0; n < Descriptor.m_Nodes.size(); ++n)
                {
                    const auto& N = Descriptor.m_Nodes[n];
                    if (N.m_Type != xlionrender::post::node_type::CUSTOM) continue;
                    const auto It = std::find_if(Types.begin(), Types.end(), [&](const auto& T) { return T.m_Id == N.m_NodeType; });
                    if (It == Types.end()) { LogMessage(xresource_pipeline::msg_type::ERROR, std::format("node {}: no system node type #{:016X} (the plugin's NodeTypes)", n, N.m_NodeType)); return xerr::create_f<state, "RenderGraph: a custom node's type is missing">(); }
                    std::string Code;
                    if (!xrender_graph::node_types::ReadText(It->m_File.parent_path() / It->m_Code, Code)) { LogMessage(xresource_pipeline::msg_type::ERROR, std::format("node {} ({}): its shader {} does not read", n, It->m_Name, It->m_Code)); return xerr::create_f<state, "RenderGraph: a node shader is missing">(); }
                    shaderc::CompileOptions Options;
                    Options.SetOptimizationLevel(shaderc_optimization_level_performance);
                    Options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
                    const std::string Source = "#version 450\n" + Frame + "\n#line 1 1\n" + Code;
                    const auto R = Compiler.CompileGlslToSpv(Source, shaderc_glsl_compute_shader, It->m_Code.c_str(), Options);
                    if (R.GetCompilationStatus() != shaderc_compilation_status_success)
                    {
                        LogMessage(xresource_pipeline::msg_type::ERROR, std::format("node {} ({}): {}", n, It->m_Name, R.GetErrorMessage()));
                        return xerr::create_f<state, "RenderGraph: a node shader does not compile (see the log)">();
                    }
                    // its parameters: "<Type>.<Param>" in the chain's table (the descriptor's own row of that name wins: the person's value), pushed as A .. E
                    if (It->m_Params.size() > 5) { LogMessage(xresource_pipeline::msg_type::ERROR, std::format("node {} ({}): at most 5 parameters", n, It->m_Name)); return xerr::create_f<state, "RenderGraph: a node type has too many parameters">(); }
                    int Custom = 0; for (std::size_t m = 0; m < n; ++m) Custom += Descriptor.m_Nodes[m].m_Type == xlionrender::post::node_type::CUSTOM;
                    auto It2 = std::find_if(B.m_Desc.m_Nodes.begin(), B.m_Desc.m_Nodes.end(), [&](const auto& Nd) { return Nd.m_Type == xlionrender::post::node_type::CUSTOM && Nd.m_Custom == Custom; });
                    for (const auto& Pp : It->m_Params)
                    {
                        const std::string Name = It->m_Name + "." + Pp.m_Name;
                        if (std::none_of(B.m_Desc.m_Params.begin(), B.m_Desc.m_Params.end(), [&](const auto& Q) { return Q.m_Name == Name; }))
                            B.m_Desc.m_Params.push_back({ Name, "", xlionrender::post::param_type::FLOAT, Pp.m_Default, Pp.m_Min, Pp.m_Max });
                        if (It2 != B.m_Desc.m_Nodes.end()) It2->m_Params.push_back(Name);
                    }
                    std::vector<std::int32_t> Words(R.cbegin(), R.cend());
                    const std::uint64_t Hash = std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char*>(Words.data()), Words.size() * 4));
                    B.m_Links.push_back({ "shader", It->m_Id, Hash });
                    B.m_Spirv.push_back(std::move(Words));
                    m_Dependencies.m_Assets.push_back(std::filesystem::relative(It->m_File, Project).wstring());                       // (a node type's file or shader edited: compile again)
                    m_Dependencies.m_Assets.push_back(std::filesystem::relative(It->m_File.parent_path() / It->m_Code, Project).wstring());
                }
            }
            // The frame stages (render graph R3): the plugin's Stages/*.stage.txt, each a library of named stages (xlionrender_stage_graph_text.h),
            // into the bundle - the renderer uses them in place of its built-in descriptions (one that does not lower: the built-in code, said once)
            {
                const std::filesystem::path Project = m_ProjectPaths.m_Project;
                const auto Dir = Project / "Cache" / "Plugins" / "xrender_graph.plugin" / "Stages";
                std::error_code Ec;
                std::vector<std::filesystem::path> Files;
                for (const auto& E : std::filesystem::directory_iterator(Dir, Ec)) if (E.path().filename().wstring().ends_with(L".stage.txt")) Files.push_back(E.path());
                std::sort(Files.begin(), Files.end());
                for (const auto& File : Files)
                {
                    xlionrender::stage::library L;
                    std::string Problem;
                    xtextfile::stream S;
                    xerr Err = S.Open(true, File.wstring(), xtextfile::file_type::TEXT);
                    if (!Err) Err = xlionrender::stage::Serialize(S, L, Problem);
                    if (Err || !Problem.empty())
                    {
                        LogMessage(xresource_pipeline::msg_type::ERROR, std::format("{}: {}", File.filename().string(), Problem.empty() ? std::string(Err.getMessage()) : Problem));
                        return xerr::create_f<state, "RenderGraph: a stage description does not read (see the log)">();
                    }
                    for (auto& [N, D] : L)
                    {
                        if (std::any_of(B.m_Stages.begin(), B.m_Stages.end(), [&](const auto& P) { return P.first == N; })) { LogMessage(xresource_pipeline::msg_type::ERROR, std::format("{}: the stage '{}' is described twice", File.filename().string(), N)); return xerr::create_f<state, "RenderGraph: a stage is described twice">(); }
                        B.m_Stages.emplace_back(std::move(N), std::move(D));
                    }
                    m_Dependencies.m_Assets.push_back(std::filesystem::relative(File, Project).wstring());     // (a stage file edited: compile again)
                }
            }
            for (auto& T : m_Target)
            {
                if (!T.m_bValid) continue;
                xtextfile::stream S;
                std::string Problem;
                if (auto Err = S.Open(false, std::wstring(T.m_DataPath), xtextfile::file_type::TEXT); Err) return Err;
                if (auto Err = B.Serialize(S, Problem); Err) return Err;
            }
            displayProgressBar("Writing the compiled chain", 1.0f);
            return {};
        }
    };
}

std::unique_ptr<xrender_graph_compiler::instance> xrender_graph_compiler::instance::Create(void)
{
    return std::make_unique<implementation>();
}
