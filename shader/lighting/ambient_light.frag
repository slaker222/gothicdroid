#version 460
#extension GL_ARB_separate_shader_objects       : enable
#extension GL_GOOGLE_include_directive          : enable
#extension GL_EXT_samplerless_texture_functions : enable

#include "lighting/tonemapping.glsl"
#include "lighting/purkinje_shift.glsl"
#include "scene.glsl"
#include "common.glsl"

layout(binding  = 0, std140) uniform UboScene {
  SceneDesc scene;
  };
layout(binding  = 1) uniform texture2D  gbufDiffuse;
layout(binding  = 2) uniform usampler2D gbufNormal;
layout(binding  = 3) uniform texture2D  irradiance;
#if defined(SSAO)
layout(binding  = 4) uniform sampler2D ssao;
#endif

layout(location = 0) out vec4 outColor;

#include "lighting/ambient_common.glsl"

float grayscale(vec3 color) {
  return dot(color, vec3(0.2125, 0.7154, 0.0721));
  }

void main() {
  const ivec2 fragCoord = ivec2(gl_FragCoord.xy);

  const vec3  diff = texelFetch(gbufDiffuse, fragCoord, 0).rgb;
  const vec3  norm = normalFetch(gbufNormal, fragCoord);

  // const vec3  linear = vec3(1);
  const vec3  linear = textureAlbedo(diff);

#if defined(SIMPLE_LIGHTING)
  // Simple lighting (GothicEngine style): fast ambient + sky irradiance + directional sun
  vec3 ambient = scene.ambient + (norm.y*0.25+0.75) * NightAmbient * Fd_Lambert;
  vec3 sky     = skyIrradiance();

  float nDotL  = clamp(dot(norm, scene.sunDir), 0.0, 1.0);
  vec3  sun    = (scene.sunDir.y > 0.0) ? (scene.sunColor * (nDotL * Fd_Lambert)) : vec3(0.0);

  vec3 luminance = sky + ambient + sun;
  vec3 color     = luminance * linear * scene.exposure;

  outColor = vec4(color, 1.0);
#else
  vec3 color = ambientColor(linear, norm);

  outColor = vec4(color, 1);
#endif
  // outColor = vec4(vec3(ao), 0);
  // outColor = vec4(linear, 0);
  // outColor = vec4(srgbEncode(linear), 0);
  }
