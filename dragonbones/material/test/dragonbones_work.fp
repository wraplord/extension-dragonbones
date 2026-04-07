in highp vec4 v_position;
in mediump vec2 v_texCoord;

out vec4 out_fragColor;

uniform mediump sampler2D u_texture;

uniform fs_uniforms
{
    mediump vec4 tint;
};

void main()
{
    // Pre-multiply alpha since all runtime textures already are
    vec4 tint_pm = vec4(tint.xyz * tint.w, tint.w);

    vec4 color = texture(u_texture, v_texCoord) * tint_pm;

    out_fragColor = color ;// vec4(color.rgb, 1.0);
}