layout(triangles, equal_spacing, cw) in;

uniform float height;

in vec3 vertex_ES[];
in vec2 tc_ES[];
in vec4 fg_Color_vf_ES[];

out vec2 tc;
out vec4 fg_Color_vf;

vec2 interpolate2D(vec2 v0, vec2 v1, vec2 v2) {
	return gl_TessCoord.x * v0 + gl_TessCoord.y * v1 + gl_TessCoord.z * v2;
}
vec3 interpolate3D(vec3 v0, vec3 v1, vec3 v2) {
	return gl_TessCoord.x * v0 + gl_TessCoord.y * v1 + gl_TessCoord.z * v2;
}

void main() {
	// Interpolate the attributes of the output vertex; vertex 2 is the tip of the grass blade
	tc          = interpolate2D(    tc_ES[0],     tc_ES[1],     tc_ES[2]);
	vec3 vertex = interpolate3D(vertex_ES[0], vertex_ES[1], vertex_ES[2]);
	vec4 epos   = fg_ModelViewMatrix * vec4(vertex, 1.0);
	vec3 base   = 0.5*(vertex_ES[0] + vertex_ES[1]);
	vec3 delta  = vertex - base;
	vertex     -= delta;
	float dist  = length(delta);
	vec3 dir    = normalize(cross(delta, (vertex_ES[0] - vertex_ES[1])));
	float mval  = clamp((2.0 - length(epos.xyz)), 0.0, 1.0)*(0.6 - pow(tc[0], 0.4));
	delta = mix(delta, dist*dir, mval);
	delta      *= dist/length(delta); // renormalize to original length
	vertex     += delta;
	fg_Color_vf = fg_Color_vf_ES[0]; // should all be the same color
	gl_Position = fg_ModelViewProjectionMatrix * vec4(vertex, 1.0);
}