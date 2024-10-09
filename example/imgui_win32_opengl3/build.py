import os

# execute this script from a "x64 Native Tools Command Prompt for VS 2022" 
# or by adding "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build" to 
# your path and execute the "vcvars64.bat" script from any shell to setup the MSVC compiler.

vst3 = True

workspace_dir = os.getcwd()
CPLUG_dir = workspace_dir + "/../.."

# flags
flags = "/nologo /LD /MD /utf-8 /Zi /EHsc /MP3 /W3"
output_name = ""
output_extension = ""

if vst3:
    flags += " /LD"
    output_name += "plugin"
    output_extension += "vst3"
else:
    output_name += "plugin"
    output_extension += "exe"

includes = " ".join([
    f"/I{CPLUG_dir}/src",
    "/Iimgui", 
    "/Iimgui/backends",
    f"/FI{workspace_dir}/config.h"
])

sources = " ".join([
    "main.cpp", 
    
    "imgui/imgui.cpp", 
    "imgui/backends/imgui_impl_win32.cpp", 
    "imgui/backends/imgui_impl_opengl3.cpp",
    
    "imgui/imgui_draw.cpp",
    "imgui/imgui_tables.cpp",
    "imgui/imgui_widgets.cpp",
])

if vst3:
    sources += f" {CPLUG_dir}/src/cplug_vst3.c"
else:
    sources += f" {CPLUG_dir}/src/cplug_standalone_win.c"
    
libs = " ".join([
    "opengl32.lib",
    "kernel32.lib",
    "user32.lib",
    "gdi32.lib",
])

if not vst3 :
    libs += " Ole32.lib"


command = f"cl {flags} {sources} /Fe:{output_name}.{output_extension} /Fd:{output_name}.pdb {includes} /link {libs}"
print(command)
return_code = os.system(command)

        
if return_code != 0: 
    print("Problems during compilation exiting")
    exit(1)

print("Cleaning")
for file in filter(lambda string : ".obj" in string, os.listdir()):
    os.remove(file)
    
print("Done")
