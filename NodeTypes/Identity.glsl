// Identity: the chain image unchanged.
vec4 Node(ivec2 p, vec2 uv) { return texelFetch(Src, p, 0); }
