# Script mode: turns a SPIR-V binary into a C header holding an array of
# 32-bit words. No external tool is needed.
#
#   cmake -DINPUT=<file.spv> -DOUTPUT=<file.h> -DSYMBOL=<name> -DSOURCE=<glsl> -P RavenEmbedSpirv.cmake

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hex_length)
math(EXPR word_count "${hex_length} / 8")

# SPIR-V is little-endian: reorder the four bytes of every word
string(REGEX REPLACE "(..)(..)(..)(..)" "0x\\4\\3\\2\\1," words "${hex}")
# eight words per line
string(REGEX REPLACE "((0x[0-9a-f]+,)(0x[0-9a-f]+,)?(0x[0-9a-f]+,)?(0x[0-9a-f]+,)?(0x[0-9a-f]+,)?(0x[0-9a-f]+,)?(0x[0-9a-f]+,)?(0x[0-9a-f]+,)?)" "\t\\1\n" words "${words}")

file(WRITE "${OUTPUT}"
"/**
 * Generated from ${SOURCE} - do not edit.
 * Regenerate with:  cmake -DRAVEN_REBUILD_SHADERS=ON
 */

static const uint32_t ${SYMBOL}[${word_count}] = {
${words}};
")
