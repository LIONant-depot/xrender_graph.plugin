@echo off
rmdir /s /q xrender_graph_compiler.vs2022
cmake ../ -G "Visual Studio 17 2022" -A x64 -B xrender_graph_compiler.vs2022

rem if no one give us any parameters then we will pause it at the end, else we are assuming that another batch file called us
if %1.==. pause
