cmake_minimum_required(VERSION 3.22)

project(DoomIIRPG_NEW CXX)

if(APPLE)
	find_library(OPENGL_FRAMEWORK OpenGL)
	# On macOS the sokol_gfx implementation must be compiled as Objective-C++
	# (sokol_gfx.h:83), for the GL backend as well as for Metal.
	enable_language(OBJCXX)
endif()

file(GLOB_RECURSE HEADER_FILES
	"*.hpp"
	"*.h")

file(GLOB_RECURSE SOURCE_FILES
	"*.cpp"
	"*.c")

# render/api   -> dr_render_core (backend-neutral, no GL, no SDL, no game deps)
# render/gl    -> dr_render_gl
# render/sokol -> dr_render_sokol
# Everything else stays in the executable, which may not compile the backend
# sources twice.
list(FILTER SOURCE_FILES EXCLUDE REGEX "/render/(api|gl|sokol)/")
list(FILTER HEADER_FILES EXCLUDE REGEX "/render/(api|gl|sokol)/")

file(GLOB RENDER_CORE_SOURCES "render/api/*.cpp")
file(GLOB RENDER_CORE_HEADERS "render/api/*.h")
add_library(dr_render_core STATIC ${RENDER_CORE_SOURCES} ${RENDER_CORE_HEADERS})
# new_src is the single include root for every target.
target_include_directories(dr_render_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB RENDER_GL_SOURCES "render/gl/*.cpp")
file(GLOB RENDER_GL_HEADERS "render/gl/*.h")
add_library(dr_render_gl STATIC ${RENDER_GL_SOURCES} ${RENDER_GL_HEADERS})
target_link_libraries(dr_render_gl PUBLIC dr_render_core OpenGL::GL)
if(APPLE)
	target_link_libraries(dr_render_gl PUBLIC ${OPENGL_FRAMEWORK})
endif()
target_link_libraries(dr_render_gl PRIVATE SDL2::SDL2)   # platform/Window.h only

# --- dr_render_sokol ---------------------------------------------------------
# The GPU API of the sokol backend is a build-time choice (spec
# 2026-09-11-sokol-gfx-backend §1.2). metal/d3d11 arrive in G6/G7, so every
# host defaults to glcore for now; forcing glcore stays supported forever
# because it is how the F12 pixel diff against dr_render_gl is run.
set(DOOM2RPG_SOKOL_BACKEND "glcore" CACHE STRING "sokol_gfx GPU API: metal | d3d11 | glcore")
set_property(CACHE DOOM2RPG_SOKOL_BACKEND PROPERTY STRINGS metal d3d11 glcore)
if(DOOM2RPG_SOKOL_BACKEND STREQUAL "metal")
	set(SOKOL_BACKEND_DEFINE SOKOL_METAL)
elseif(DOOM2RPG_SOKOL_BACKEND STREQUAL "d3d11")
	set(SOKOL_BACKEND_DEFINE SOKOL_D3D11)
elseif(DOOM2RPG_SOKOL_BACKEND STREQUAL "glcore")
	set(SOKOL_BACKEND_DEFINE SOKOL_GLCORE)
else()
	message(FATAL_ERROR "DOOM2RPG_SOKOL_BACKEND must be metal, d3d11 or glcore (got '${DOOM2RPG_SOKOL_BACKEND}')")
endif()

# sokol-shdc compiles the annotated GLSL into the BUILD tree; generated code
# never enters new_src (ADR 0024 point 5). An unsupported host fails here with
# a FATAL_ERROR from sokol_shaders.cmake, by design: there is no committed
# header fallback.
# The prebuilts live in a git submodule (see .gitmodules); a clone made without
# --recursive leaves the directory empty, so check before include() to get a
# useful message instead of "include could not find requested file".
set(SOKOL_TOOLS_DIR "${PROJECT_SOURCE_DIR}/../third_party_libs/sokol-tools-bin")
if(NOT EXISTS "${SOKOL_TOOLS_DIR}/sokol_shaders.cmake")
	message(FATAL_ERROR
		"sokol-shdc prebuilts not found at\n"
		"    ${SOKOL_TOOLS_DIR}\n"
		"third_party_libs/sokol-tools-bin is a git submodule and is not "
		"initialized. Run in the repository root:\n"
		"    git submodule update --init --recursive\n"
		"then re-run CMake.")
endif()
include("${SOKOL_TOOLS_DIR}/sokol_shaders.cmake")
# wgsl and spirv_vk are left out on purpose: we never build SOKOL_WGPU/VULKAN.
set(SG_SLANG "glsl410:glsl300es:metal_macos:metal_ios:metal_sim:hlsl5")
sokol_shader("render/sokol/shaders/quad2d.glsl" "${SG_SLANG}")
sokol_shader("render/sokol/shaders/world.glsl" "${SG_SLANG}")
set(SG_GENERATED
	"${CMAKE_CURRENT_BINARY_DIR}/compile_shaders/render/sokol/shaders/quad2d.glsl.h"
	"${CMAKE_CURRENT_BINARY_DIR}/compile_shaders/render/sokol/shaders/world.glsl.h")

file(GLOB RENDER_SOKOL_SOURCES "render/sokol/*.cpp")
file(GLOB RENDER_SOKOL_HEADERS "render/sokol/*.h")
if(APPLE)
	# Objective-C++ implementation instead of the plain C++ one.
	list(FILTER RENDER_SOKOL_SOURCES EXCLUDE REGEX "/SokolGfxImpl\\.cpp$")
	list(APPEND RENDER_SOKOL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/render/sokol/SokolGfxImpl.mm")
	# *.mm is not in the GLOB patterns at all, so the Metal environment has to
	# be listed by hand as well (spec §6.4).
	if(DOOM2RPG_SOKOL_BACKEND STREQUAL "metal")
		list(APPEND RENDER_SOKOL_SOURCES
			"${CMAKE_CURRENT_SOURCE_DIR}/render/sokol/SgEnvironmentMetal.mm")
	endif()
	# ARC for the Objective-C++ sources only: it is what gives the CAMetalDrawable
	# and MTLDevice members their lifetime, without hand-rolled retain/release.
	# sokol_gfx supports both modes (sokol_gfx.h:7824-7832).
	set_source_files_properties(
		"${CMAKE_CURRENT_SOURCE_DIR}/render/sokol/SokolGfxImpl.mm"
		"${CMAKE_CURRENT_SOURCE_DIR}/render/sokol/SgEnvironmentMetal.mm"
		PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()
add_library(dr_render_sokol STATIC
	${RENDER_SOKOL_SOURCES} ${RENDER_SOKOL_HEADERS} ${SG_GENERATED})
target_link_libraries(dr_render_sokol PUBLIC dr_render_core)
target_link_libraries(dr_render_sokol PRIVATE SDL2::SDL2)   # platform/Window.h only
# PUBLIC: core/RenderBackendFactory.cpp and core/AppContext.cpp include
# SgRenderBackend.h / SgEnvironment.h, which declare (never implement) sokol.
target_include_directories(dr_render_sokol PUBLIC
	"${PROJECT_SOURCE_DIR}/../third_party_libs/sokol")
target_compile_definitions(dr_render_sokol PUBLIC ${SOKOL_BACKEND_DEFINE})
target_include_directories(dr_render_sokol PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}/compile_shaders/render/sokol/shaders")
if(DOOM2RPG_SOKOL_BACKEND STREQUAL "glcore")
	if(APPLE)
		target_link_libraries(dr_render_sokol PUBLIC ${OPENGL_FRAMEWORK})
	else()
		target_link_libraries(dr_render_sokol PUBLIC OpenGL::GL)
	endif()
elseif(DOOM2RPG_SOKOL_BACKEND STREQUAL "metal")
	target_link_libraries(dr_render_sokol PUBLIC
		"-framework Metal" "-framework QuartzCore" "-framework Foundation")
endif()

add_executable(DoomIIRPG ${HEADER_FILES} ${SOURCE_FILES})

target_link_libraries(DoomIIRPG
	dr_render_gl
	dr_render_sokol
	SDL2::SDL2
	ZLIB::ZLIB
	OpenGL::GL
)

if(APPLE)
	target_link_libraries(DoomIIRPG ${OPENGL_FRAMEWORK})
endif()

set_target_properties(DoomIIRPG PROPERTIES
	OUTPUT_NAME DoomIIRPG
)

# Same language level and dialect flags for every target.
foreach(tgt DoomIIRPG dr_render_core dr_render_gl dr_render_sokol)
	set_target_properties(${tgt} PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
		OBJCXX_STANDARD 17
		OBJCXX_STANDARD_REQUIRED ON
		OBJCXX_EXTENSIONS OFF
	)
	if(NOT MSVC)
		target_compile_options(${tgt} PRIVATE
			-fsigned-char
			-Wno-write-strings
			$<$<PLATFORM_ID:Darwin>:-Wno-deprecated-declarations>
		)
	endif()
	target_include_directories(${tgt} PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
	)
endforeach()

# Copy the game data archive next to the executable for easy local runs.
set(DOOM_IPA_CANDIDATES
	"${CMAKE_SOURCE_DIR}/build/src/Doom 2 RPG.ipa"
	"${CMAKE_SOURCE_DIR}/Doom 2 RPG.ipa"
	"${CMAKE_SOURCE_DIR}/doom_ii_rpg.zip")
set(DOOM_IPA_SRC "")
foreach(candidate ${DOOM_IPA_CANDIDATES})
	if(EXISTS "${candidate}")
		set(DOOM_IPA_SRC "${candidate}")
		break()
	endif()
endforeach()
if(DOOM_IPA_SRC)
	add_custom_command(TARGET DoomIIRPG POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${DOOM_IPA_SRC}"
			"$<TARGET_FILE_DIR:DoomIIRPG>/Doom 2 RPG.ipa"
	)
endif()
