// The compiler of the RenderGraph resource: its descriptor (the post chain's nodes and parameters) in, checked the way the renderer will
// lower it (post::Lower: one tone map and last, TAA first, each node once, every node's parameters declared, their sources known), out
// the compiled description - post::Serialize's text, what the render module loads into a Level's view. An invalid chain is an error
// naming the node or parameter (the renderer would otherwise tone map only).
#include "dependencies/xstrtool/source/xstrtool.h"
#include "plugins/xrender_graph.plugin/source/Module/xrender_graph_descriptor.h"
#include "dependencies/xLIONRender/src/xlionrender_post_graph_text.h"
#include "source/Compiler/xrender_graph_compiler.h"

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
            auto D = Descriptor.ToDesc();
            for (auto& T : m_Target)
            {
                if (!T.m_bValid) continue;
                xtextfile::stream S;
                std::string Problem;
                if (auto Err = S.Open(false, std::wstring(T.m_DataPath), xtextfile::file_type::TEXT); Err) return Err;
                if (auto Err = xlionrender::post::Serialize(S, D, Problem); Err) return Err;
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
