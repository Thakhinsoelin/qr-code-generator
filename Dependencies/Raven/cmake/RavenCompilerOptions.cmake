# Warning levels shared by the library, the examples and the tests.

function(raven_set_compiler_options target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /utf-8)
		# the C runtime "secure" variants are not portable
		target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
		if(RAVEN_WARNINGS_AS_ERRORS)
			target_compile_options(${target} PRIVATE /WX)
		endif()
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow -Wstrict-prototypes -Wpedantic)
		if(RAVEN_WARNINGS_AS_ERRORS)
			target_compile_options(${target} PRIVATE -Werror)
		endif()
	endif()
endfunction()
