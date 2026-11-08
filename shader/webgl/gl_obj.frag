#version 450

layout(binding = 0, std140) uniform Scene {
  vec4 sunDir;   // xyz: direction to the sun
  vec4 sunColor;
  vec4 ambient;  // w: alpha multiplier of blended materials
  } scene;

layout(binding = 2) uniform sampler2D textureD;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec4 outColor;

void main() {
  vec4 c = texture(textureD, inUv);
#if defined(ATEST)
  if(c.a<0.5)
    discard;
#endif
#if defined(EMISSIVE)
  // multiply (blob shadows, decals) and additive materials: the texture as it is, as in the material pipelines
  outColor = c;
#else
  const float l = max(dot(normalize(inNormal), scene.sunDir.xyz), 0.0);
  outColor = vec4(c.rgb*(scene.ambient.rgb + scene.sunColor.rgb*l), c.a);
#endif
  }
