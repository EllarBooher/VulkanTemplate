message(STATUS "vulkan_template_lib - begin")

add_library(
	vulkan_template_lib
	STATIC 
	"source/vulkan_template/VulkanTemplate.cpp"
	
	"source/vulkan_template/core/Log.cpp"
	"source/vulkan_template/core/UIWindowScope.cpp"

	"source/vulkan_template/app/DescriptorAllocator.cpp"
	"source/vulkan_template/app/FrameBuffer.cpp" 
	"source/vulkan_template/app/GraphicsContext.cpp" 
	"source/vulkan_template/app/Swapchain.cpp" 
	"source/vulkan_template/app/UILayer.cpp" 
	"source/vulkan_template/app/PlatformWindow.cpp"
	"source/vulkan_template/app/RenderTarget.cpp"
	"source/vulkan_template/app/Renderer.cpp"
	"source/vulkan_template/app/PostProcess.cpp" 
	"source/vulkan_template/app/PropertyTable.cpp"
	"source/vulkan_template/app/Mesh.cpp" 
	"source/vulkan_template/app/GBuffer.cpp"
	"source/vulkan_template/app/Scene.cpp"
	"source/vulkan_template/app/LightingPass.cpp"

	"source/vulkan_template/vulkan/Image.cpp" 
	"source/vulkan_template/vulkan/ImageView.cpp" 
	"source/vulkan_template/vulkan/ImageOperations.cpp"
	"source/vulkan_template/vulkan/VulkanUsage.cpp" 
	"source/vulkan_template/vulkan/VulkanStructs.cpp"
	"source/vulkan_template/vulkan/Shader.cpp"
	"source/vulkan_template/vulkan/Pipeline.cpp"
	"source/vulkan_template/vulkan/BufferAllocation.cpp"
	"source/vulkan_template/vulkan/Buffers.cpp" 
	"source/vulkan_template/vulkan/Immediate.cpp"
	
	"source/vulkan_template/platform/PlatformUtilsWindows.cpp"
)

add_dependencies(vulkan_template_lib shaders)

target_include_directories(
	vulkan_template_lib
	PRIVATE 
		"${CMAKE_CURRENT_SOURCE_DIR}/source"
	PUBLIC 
		"${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(
	vulkan_template_lib
	PRIVATE
		$<$<CONFIG:Debug>:VKT_DEBUG_BUILD>
		$<$<CONFIG:RelWithDebInfo>:VKT_DEBUG_BUILD>
		GLFW_INCLUDE_VULKAN
		GLM_ENABLE_EXPERIMENTAL
		GLM_FORCE_DEPTH_ZERO_TO_ONE
		GLM_FORCE_SIZE_T_LENGTH
		GLM_FORCE_RADIANS
		GLM_FORCE_EXPLICIT_CTOR
# Volk metaloader will load the methods for us
		VK_NO_PROTOTYPES
)

##### Dear ImGui #####

FetchContent_MakeAvailable(imgui)

add_library(
	imgui
	STATIC
		"${imgui_SOURCE_DIR}/imgui.cpp"
		"${imgui_SOURCE_DIR}/imgui_demo.cpp"
		"${imgui_SOURCE_DIR}/imgui_draw.cpp"
		"${imgui_SOURCE_DIR}/imgui_tables.cpp"
		"${imgui_SOURCE_DIR}/imgui_widgets.cpp"
		"${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
		"${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
		"${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
 )
target_include_directories(
	imgui 
	SYSTEM
	PUBLIC 
		"${imgui_SOURCE_DIR}"
		"${imgui_SOURCE_DIR}/backends"
)
target_include_directories(
	imgui
	PUBLIC
# Path to our imguiconfig.h
		"${CMAKE_SOURCE_DIR}/third_party/imgui"
)
target_compile_definitions(
	imgui
	PRIVATE
# We use volk so we are dynamically linking the vulkan loader
		VK_NO_PROTOTYPES 
	PUBLIC
		IMGUI_USER_CONFIG="imguiconfig.h"
)
target_link_libraries(
	imgui
	PRIVATE
# link for GLFW and Vulkan backends
		volk
		glfw
	INTERFACE
# Indirect dependency on glm due to glm types in imguiconfig
		glm
)

##### implot #####

FetchContent_MakeAvailable(implot)

add_library(
	implot
	STATIC
		"${implot_SOURCE_DIR}/implot.cpp"
		"${implot_SOURCE_DIR}/implot_items.cpp"
		"${implot_SOURCE_DIR}/implot_internal.h"
)
target_include_directories(
	implot 
	PUBLIC 
		"${implot_SOURCE_DIR}"
)
target_link_libraries(
	implot
	PRIVATE
		imgui
)

##### fastglTF #####

FetchContent_MakeAvailable(fastgltf)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	target_compile_definitions(
		fastgltf
		PRIVATE
			_SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
	)
endif()

##### spirv-reflect #####

# When using spirv-reflect's CMakeLists, spirv-reflect.h could not be found 
# (in VS2022) unless configuring CMake twice after deleting the cache.
# So we add it ourselves.
set(SPIRV-REFLECT_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/spirv-reflect")
add_library(
	spirv-reflect
	STATIC
		"${SPIRV-REFLECT_SOURCE_DIR}/spirv_reflect.cpp"
		"${SPIRV-REFLECT_SOURCE_DIR}/spirv_reflect.h"
)
target_include_directories(
	spirv-reflect 
	PUBLIC 
		"${SPIRV-REFLECT_SOURCE_DIR}"
)

##### stb #####

# We could use FetchContent, but the stb repo is quite large and contains many 
# things we will not be using.

set(STB_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/stb")
add_library(
	stb 
	INTERFACE
)
target_include_directories(
	stb 
	INTERFACE 
		"${STB_SOURCE_DIR}/include"
)

target_compile_definitions(
	vulkan_template_lib
	PRIVATE
		STBI_MAX_DIMENSIONS=2048
)

###############

# These dependencies require no additional configuration

FetchContent_MakeAvailable(glm)
FetchContent_MakeAvailable(VulkanMemoryAllocator)
FetchContent_MakeAvailable(glfw)
FetchContent_MakeAvailable(vk-bootstrap)
FetchContent_MakeAvailable(volk)
FetchContent_MakeAvailable(spdlog)

target_link_libraries(
	vulkan_template_lib
	PRIVATE
		spirv-reflect
		glm
		VulkanMemoryAllocator
		glfw
		imgui
		implot
		vk-bootstrap
		fastgltf
		volk
		stb
		spdlog::spdlog
)