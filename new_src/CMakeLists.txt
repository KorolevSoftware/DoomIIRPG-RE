cmake_minimum_required(VERSION 3.22)

project(DoomIIRPG_NEW CXX)

file(GLOB_RECURSE HEADER_FILES
	"*.hpp"
	"*.h")

file(GLOB_RECURSE SOURCE_FILES
	"*.cpp"
	"*.c")

add_executable(DoomIIRPG ${HEADER_FILES} ${SOURCE_FILES})

set_target_properties(DoomIIRPG PROPERTIES
	OUTPUT_NAME DoomIIRPG
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

if(NOT MSVC)
	target_compile_options(DoomIIRPG PRIVATE
		-fsigned-char
		-Wno-write-strings
		$<$<PLATFORM_ID:Darwin>:-Wno-deprecated-declarations>
	)
endif()

target_include_directories(DoomIIRPG PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(DoomIIRPG
	SDL2::SDL2
	ZLIB::ZLIB
	OpenGL::GL
)

if(APPLE)
	find_library(OPENGL_FRAMEWORK OpenGL)
	target_link_libraries(DoomIIRPG ${OPENGL_FRAMEWORK})
endif()

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
