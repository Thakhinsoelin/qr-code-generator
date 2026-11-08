/**
 * Raven example - effects
 * The effects that come with Raven, and two effects of the application.
 *
 *   <Border Effect="aurora" EffectColor="#30D158" EffectParams="1,1,1.5"/>
 *   <Border Effect="wave.spv"/>                     a shader of the application (wave.frag)
 *   <Border Effect="liquid.spv" EffectBackdrop="true"/>   one that reads what is behind it
 *
 * The .spv files are compiled from the .frag files next to them:
 *   glslangValidator -V -I../../include/raven/shaders wave.frag -o wave.spv
 * docs/shaders.html is the guide.
 */

#include "../common/example.h"

#ifndef RAVEN_EXAMPLE_DIR
#define RAVEN_EXAMPLE_DIR "."
#endif

int main(void) {
	Rid window;
	if (!example_init("Raven effects")) return 1;
	Rtheme_apply(R_THEME_DARK);
	window = Rwindow_create_from_xml_file(RAVEN_EXAMPLE_DIR "/effects.xml");
	if (window == R_INVALID_ID) {
		Rshutdown();
		return 1;
	}
	if (!example_automated(window)) {
		while (Rwait_frame(-1.0)) {
		}
	}
	Rshutdown();
	return 0;
}
