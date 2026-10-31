#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "scene.glsl"
#include "common.glsl"

layout(location = 0) out vec4 outColor;

layout(binding = 0, std140) uniform UboScene {
  SceneDesc scene;
  };
layout(binding = 1) uniform sampler2D zbuffer;
layout(binding = 2) uniform sampler2D swampMask;

// fixme: copy-paste
vec4 waterScatter(vec3 back, vec3 normal, const float len, const float swamp, const float up) {
  const float depth         = len / 5000.0; // 50 meters
  // a constant tint even right at the lens: blue water, green swamp; looking up at the surface is darker
  const vec3  tint          = mix(vec3(0.50,0.78,1.0), vec3(0.62,0.85,0.40), swamp) * mix(1.0, 0.55, up);
  const vec3  transmittance = exp(-depth * mix(vec3(4,2,1), vec3(6,1.5,5), swamp) * 1.25) * tint;
#if defined(SCATTERING)
  const float f       = fresnel(scene.sunDir,normal,IorWater);
  const vec3  scatter = f * scene.sunColor * (1-exp(-len/20000.0)) * scene.exposure * 0.5;
  return vec4(scatter*transmittance, 1);
#else
  return vec4(transmittance, 1);
#endif
  }

vec3 unproject(vec4 screen) {
  const vec4 pos4 = scene.viewProjectInv * screen;
  return pos4.xyz/pos4.w;
  }

void main() {
  const vec2  fragCoord = (gl_FragCoord.xy*scene.screenResInv)*2.0-vec2(1.0);
  const float depth     = texelFetch(zbuffer,  ivec2(gl_FragCoord.xy), 0).r;

  const vec3  camPos    = unproject(vec4(0,0,0, 1.0));
  const vec3  wPos      = unproject(vec4(fragCoord.x, fragCoord.y, depth, 1.0));

  const float len = length(wPos-camPos);
  const float swamp = scene.swampWater*textureLod(swampMask, (camPos.xz - scene.swampRect.xy)*scene.swampRect.zw, 0.0).r;
  const float up    = clamp((wPos.y-camPos.y)/max(len,1.0)*2.0, 0.0, 1.0); // the view goes up to the surface
  outColor = waterScatter(vec3(1),vec3(0,1,0),len,swamp,up);
  }
