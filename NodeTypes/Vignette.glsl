// Vignette: the chain image darkened toward the corners (a smooth falloff from the centre). Push.m_A: its Strength parameter.
vec4 Node(ivec2 p, vec2 uv)
{
    const vec4  c = texelFetch(Src, p, 0);
    const vec2  d = uv * 2.0 - 1.0;
    const float v = 1.0 - Push.m_A * smoothstep(0.4, 1.4, dot(d, d));
    return vec4(c.rgb * v, c.a);
}
