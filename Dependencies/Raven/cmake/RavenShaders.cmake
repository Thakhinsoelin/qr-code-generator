# Recompiles the GLSL shaders of the Vulkan backend into the SPIR-V headers
# kept in the repository (src/render/vulkan/generated). Only used when
# RAVEN_REBUILD_SHADERS is ON: a normal build needs no shader compiler.

find_program(RAVEN_GLSLANG NAMES glslangValidator glslang HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin bin)
find_program(RAVEN_GLSLC NAMES glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin bin)
if(NOT RAVEN_GLSLANG AND NOT RAVEN_GLSLC)
	message(FATAL_ERROR "RAVEN_REBUILD_SHADERS needs glslangValidator or glslc (both come with the Vulkan SDK)")
endif()

# raven_compile_shader(<source.glsl> <output header> <C symbol>)
function(raven_compile_shader source header symbol)
	set(source_path "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
	set(header_path "${CMAKE_CURRENT_SOURCE_DIR}/${header}")
	get_filename_component(name "${source}" NAME)
	set(spirv "${CMAKE_CURRENT_BINARY_DIR}/${name}.spv")
	if(RAVEN_GLSLANG)
		set(compile ${RAVEN_GLSLANG} -V --quiet -I${CMAKE_CURRENT_SOURCE_DIR}/include/raven/shaders -o "${spirv}" "${source_path}")
	else()
		set(compile ${RAVEN_GLSLC} -O -I${CMAKE_CURRENT_SOURCE_DIR}/include/raven/shaders -o "${spirv}" "${source_path}")
	endif()
	add_custom_command(
		OUTPUT "${header_path}"
		COMMAND ${compile}
		COMMAND ${CMAKE_COMMAND} -DINPUT=${spirv} -DOUTPUT=${header_path} -DSYMBOL=${symbol} -DSOURCE=${source}
			-P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RavenEmbedSpirv.cmake"
		DEPENDS "${source_path}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RavenEmbedSpirv.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/include/raven/shaders/raven_effect.glsl"
		COMMENT "Raven: compiling ${source}"
		VERBATIM
	)
	# (the library is made after the shaders are listed: it takes these headers then)
	set_property(GLOBAL APPEND PROPERTY RAVEN_SHADER_HEADERS "${header_path}")
endfunction()
