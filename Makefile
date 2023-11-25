WINDOWS_CURDIR:= $(subst /,\,$(CURDIR))
WORKSPACEFOLDER:=${CURDIR}

BUILD_PATH:=${WORKSPACEFOLDER}/build
SHADERS_PATH:=${WORKSPACEFOLDER}/shaders
SRCS:=$(wildcard ${WORKSPACEFOLDER}/src/*.cpp)
INCLUDES:=$(wildcard ${WORKSPACEFOLDER}/include/*.h)

INCLUDE_PATH:=/I${WORKSPACEFOLDER}/include/ /I"C:/Libraries/glfw-3.3.8.bin.WIN64/include" /I"C:\Libraries\VulkanSDK\1.3.250.1\Include"
INCLUDE_PATH+=/I${WORKSPACEFOLDER}/exts/imgui /I"C:\Libraries\SDL2-devel-2.28.5-VC\SDL2-2.28.5\include" 
INCLUDE_PATH+=/I${WORKSPACEFOLDER}/exts/glTF

SDL_LIB_PATH:="C:\Libraries\SDL2-devel-2.28.5-VC\SDL2-2.28.5\lib\x64"
#SDL_LIB_PATH:="C:\SDL2-2.28.5\VisualC\x64\Release"
LIB_PATH:=/LIBPATH:"C:\Libraries\glfw-3.3.8.bin.WIN64\lib-static-ucrt" /LIBPATH:"C:\Libraries\VulkanSDK\1.3.250.1\Lib" /LIBPATH:${SDL_LIB_PATH}
LIBS:=/link ${LIB_PATH} vulkan-1.lib SDL2main.lib SDL2.lib shell32.lib

SHADERS:=${SHADERS_PATH}/spv/vertshader.spv ${SHADERS_PATH}/spv/fragshader.spv
IMGUI_SRCS:=${wildcard ${WORKSPACEFOLDER}/exts/imgui/*.cpp}
IMGUI_OBJS:=${patsubst ${WORKSPACEFOLDER}/exts/imgui/%.cpp,${BUILD_PATH}/%.obj,${IMGUI_SRCS}}

all:${BUILD_PATH}/main.exe ${SHADERS}

${BUILD_PATH}/main.exe:${SRCS} ${INCLUDES} ${IMGUI_OBJS}
	@cl /std:c++20 ${INCLUDE_PATH} /EHsc /Zi /Fo${BUILD_PATH}/ /Fe${BUILD_PATH}/main.exe /Fd${BUILD_PATH}/main.pdb ${SRCS} ${IMGUI_OBJS} ${LIBS} 

${BUILD_PATH}/%.obj:${WORKSPACEFOLDER}/exts/imgui/%.cpp
	@cl /EHsc /Zi ${INCLUDE_PATH} /Fo${BUILD_PATH}/ /Fd${BUILD_PATH}/$*.pdb -c $< ${LIBS} 

${SHADERS_PATH}/spv/%.spv:${SHADERS_PATH}/glsl/%.*
	${VULKAN_SDK}/Bin/glslc.exe $< -o $@

clean:
	del ${WINDOWS_CURDIR}\build\*.exe
	del ${WINDOWS_CURDIR}\build\*.pdb
	del ${WINDOWS_CURDIR}\build\*.ilk
	del ${WINDOWS_CURDIR}\build\*.obj