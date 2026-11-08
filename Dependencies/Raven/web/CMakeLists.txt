# Raven Web: web pages inside a Raven interface (see include/raven_web.h).
#
# The web sources are compiled into the Raven library itself (see the root
# CMakeLists.txt, RAVEN_BUILD_WEB): there is one library, raven::raven.
# raven::web stays as a name for the programs that already link with it:
# it brings nothing more than raven::raven.
#
#   target_link_libraries(app PRIVATE raven::raven)       # #include <raven_web.h>

add_library(raven_web INTERFACE)
target_link_libraries(raven_web INTERFACE raven)
set_target_properties(raven_web PROPERTIES EXPORT_NAME web)
add_library(raven::web ALIAS raven_web)

install(TARGETS raven_web EXPORT ravenTargets)
install(FILES include/raven_web.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(NOT RAVEN_WITH_STB_IMAGE)
	message(WARNING "raven_web: Raven is built without its image decoder (RAVEN_WITH_STB_IMAGE=OFF): web views will work but show nothing, the pictures of the pages cannot be decoded.")
endif()

# The test: the parts that need no browser always run; the rest is reported
# as skipped on a machine without a browser of the Chromium family.
if(RAVEN_BUILD_TESTS)
	add_executable(test_web tests/test_web.c src/web_json.c)
	target_link_libraries(test_web PRIVATE raven::raven)
	target_include_directories(test_web PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../tests")
	set_target_properties(test_web PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON FOLDER "tests")
	if(NOT WIN32)
		target_link_libraries(test_web PRIVATE m)
	endif()
	if(MSVC)
		target_compile_definitions(test_web PRIVATE _CRT_SECURE_NO_WARNINGS)
	endif()
	if(NOT RAVEN_WITH_STB_IMAGE)
		target_compile_definitions(test_web PRIVATE RAVEN_TEST_NO_IMAGE_DECODER)
	endif()
	add_test(NAME test_web COMMAND test_web)
	set_tests_properties(test_web PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
endif()
