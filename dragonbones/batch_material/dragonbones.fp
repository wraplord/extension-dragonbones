#version 140


//precision mediump float;
varying mediump vec2 v_texCoord;
uniform mediump sampler2D u_texture;
uniform fs_uniforms
{
    mediump vec4 tint;
};


out vec2 out_fragColor;

void main() {
    //gl_FragColor = texture2D(u_texture, v_texCoord);
    mediump vec4 tint_pm = vec4(tint.xyz * tint.w, tint.w);
    
    gl_FragColor = texture(u_texture, v_texCoord.xy) * tint_pm;
}