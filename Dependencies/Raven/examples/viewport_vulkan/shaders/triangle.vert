#version 450

/* Raven example - a rotating triangle without any vertex buffer. */

layout(push_constant) uniform PushConstants {
	float angle;
	/* width / height of the viewport */
	float aspect;
} pc;

layout(location = 0) out vec3 v_color;

const vec2 positions[3] = vec2[3](vec2(0.0, -0.7), vec2(0.65, 0.5), vec2(-0.65, 0.5));
const vec3 colors[3] = vec3[3](vec3(1.0, 0.35, 0.2), vec3(0.25, 0.85, 0.4), vec3(0.25, 0.5, 1.0));

void main() {
	vec2 p = positions[gl_VertexIndex];
	float c = cos(pc.angle), s = sin(pc.angle);
	p = vec2(p.x * c - p.y * s, p.x * s + p.y * c);
	p.x /= max(pc.aspect, 0.001);
	v_color = colors[gl_VertexIndex];
	gl_Position = vec4(p, 0.0, 1.0);
}
