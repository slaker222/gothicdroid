#version 450

// WebGL 2 renderer: the world drawn at the render resolution, stretched onto the screen

layout(binding = 0) uniform sampler2D src;

layout(location = 0) in  vec2 UV;
layout(location = 0) out vec4 outColor;

void main() {
  outColor = vec4(texture(src, UV).rgb, 1.0);
  }
