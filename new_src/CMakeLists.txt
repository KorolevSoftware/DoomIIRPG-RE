cmake_minimum_required(VERSION 3.22)

project(DoomIIRPG_NEW CXX)

if(APPLE)
	find_library(OPENGL_FRAMEWORK OpenGL)
endif()

file(GLOB_RECURSE HEADER_FILES
	"*.hpp"
	"*.h")

file(GLOB_RECURSE SOURCE_FILES
	"*.cpp"
	"*.c")

# render/api  -> dr_render_core (backend-neutral, no GL, no SDL, no game deps)
# render/gl   -> dr_render_gl
# render/sdl  -> dr_render_sdl
# Everything else stays in the executable, which may not compile the backend
# sources twice.
list(FILTER SOURCE_FILES EXCLUDE REGEX "/render/(api|gl|sdl)/")
list(FILTER HEADER_FILES EXCLUDE REGEX "/render/(api|gl|sdl)/")

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

file(GLOB RENDER_SDL_SOURCES "render/sdl/*.cpp")
file(GLOB RENDER_SDL_HEADERS "render/sdl/*.h")
add_library(dr_render_sdl STATIC ${RENDER_SDL_SOURCES} ${RENDER_SDL_HEADERS})
target_link_libraries(dr_render_sdl PUBLIC dr_render_core SDL2::SDL2)

add_executable(DoomIIRPG ${HEADER_FILES} ${SOURCE_FILES})

target_link_libraries(DoomIIRPG
	dr_render_gl
	dr_render_sdl
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
foreach(tgt DoomIIRPG dr_render_core dr_render_gl dr_render_sdl)
	set_target_properties(${tgt} PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
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
