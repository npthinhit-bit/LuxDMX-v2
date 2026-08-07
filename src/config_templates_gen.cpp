// Compiles the board-default config templates into the firmware.
//
// The template DATA is generated from templates/*.ini into
// generated/config_templates.gen.h at build time (extra_scripts.py ->
// tools/gen_config_templates.py). This tiny committed wrapper pulls that
// data into the build: it lives in src/ and is always present in the source
// file list, so the generated header is always compiled.
#include "generated/config_templates.gen.h"
