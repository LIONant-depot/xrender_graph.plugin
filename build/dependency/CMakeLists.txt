include(${CMAKE_BINARY_DIR}/_deps/xcmake_tools/Common.cmake)

# Fetch dependencies
FetchAndPopulate("https://github.com/LIONant-depot/xresource_pipeline_v2.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xstrtool.git" "main" )

DefineInterfaceComponent(xrender_graph_compiler_ "dependencies"
  "source/Module/xrender_graph_descriptor.h"
)

