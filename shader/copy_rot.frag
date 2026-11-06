#version 450
#extension GL_ARB_separate_shader_objects : enable

// android pre-rotation: the frame drawn in landscape (src) is copied turned into the swapchain image of the panel's
// (portrait) orientation; every pixel is copied as it is
layout(binding = 0) uniform sampler2D src;

layout(push_constant, std140) uniform Push {
  ivec2 size;     // of src
  int   rotation; // 90 or 270
  } push;

layout(location = 0) out vec4 outColor;

void main() {
  const ivec2 p = ivec2(gl_FragCoord.xy);
  ivec2 s;
  if(push.rotation==90)
    s = ivec2(p.y, push.size.y-1-p.x); else
    s = ivec2(push.size.x-1-p.y, p.x);
  outColor = texelFetch(src, s, 0);
  }
