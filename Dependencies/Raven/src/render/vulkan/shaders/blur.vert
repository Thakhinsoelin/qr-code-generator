#version 450

/*
 * Raven - full-screen triangle used by the passes that build the backdrop of
 * the glass primitives (blur.frag). No vertex buffer.
 */

layout(location = 0) out vec2 v_uv;

void main() {
	vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
	v_uv = corner;
	gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
