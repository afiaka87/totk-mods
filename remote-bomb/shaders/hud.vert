#version 450 core
layout(std140,binding=0) uniform Hud { vec4 rect,gauge,tint; };
layout(location=0) out vec2 uv;
void main(){
    float v=float(gl_VertexID);
    float x=(v<.5||(v>1.5&&v<3.5))?0:1;
    float y=((v>1.5&&v<3.5)||v>4.5)?1:0;
    uv=vec2(x,y);
    vec2 pixel=rect.xy+(uv*2-1)*rect.zw;
    gl_Position=vec4(pixel.x/640-1,1-pixel.y/360,0,1);
}
