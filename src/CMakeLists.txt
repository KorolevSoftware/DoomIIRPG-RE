file(GLOB_RECURSE HEADER_FILES
	"*.hpp"
	"*.h")

file(GLOB_RECURSE SOURCE_FILES
	"*.cpp"
	"*.c")

file(GLOB_RECURSE RESOURCES_FILES
	"*.rc"
	"*.ico")

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header Files" FILES ${HEADER_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source Files" FILES ${SOURCE_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Resources Files" FILES ${RESOURCES_FILES})

add_executable(DoomIIRPG_Legacy ${HEADER_FILES} ${SOURCE_FILES} ${RESOURCES_FILES})

set_target_properties(DoomIIRPG_Legacy PROPERTIES
    OUTPUT_NAME DoomIIRPG_Legacy
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_link_libraries(DoomIIRPG_Legacy
	SDL2::SDL2
	ZLIB::ZLIB
	OpenAL::OpenAL
	OpenGL::GL
	${HASH_LIBRARY_TGT_NAME})
