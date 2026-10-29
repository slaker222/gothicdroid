#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive    : enable

#include "scene.glsl"
#include "common.glsl"
#include "lighting/tonemapping.glsl"

layout(binding = 0, std140) uniform UboScene {
  SceneDesc scene;
  };
layout(binding = 1) uniform sampler2D sunSprite;
layout(binding = 2) uniform sampler2D tLUT;

layout(push_constant, std430) uniform UboPush {
  vec2  sunPos;
  vec2  sunSz;
  vec3  sunDir;
  float GSunIntensity;
  mat4  viewProjectInv;
  uint  isSun;
  } push;

layout(location = 0) in  vec2 uv;
layout(location = 1) in  vec2 inPos;

layout(location = 0) out vec4 outColor;

vec3 project(mat4 m, vec3 pos) {
  vec4 p = m*vec4(pos,1);
  return p.xyz/p.w;
  }

// 5.5.2. LUT parameterization
vec3 textureLUT(sampler2D tex, vec3 pos, vec3 sunDir) {
  float height   = length(pos);
  vec3  up       = pos / height;
  float cosAngle = dot(sunDir, up);

  vec2 uv;
  uv.x = 0.5 + 0.5*cosAngle;
  uv.y = (height - RPlanet)/(RAtmos - RPlanet);
  return texture(tex, uv).rgb;
  }

void main() {
  const vec3 pos  = vec3(0, RPlanet+1, 0);
  const vec3 pos0 = project(push.viewProjectInv, vec3(inPos,0.0));
  const vec3 pos1 = project(push.viewProjectInv, vec3(inPos,1.0));
  const vec3 view = normalize(pos1);

  vec4 clr = texture(sunSprite, uv);
  vec3 lum;
  if(push.isSun!=0) {
    // bright disc over a soft halo, drawn after the fog; the disc is opaque and burns out to white
    const float r    = length(uv*2.0 - 1.0);
    const float disc = 1.0 - smoothstep(0.10, 0.15, r);
    const vec3  core = mix(vec3(1.0,0.97,0.90), vec3(1.0,0.80,0.45), smoothstep(0.05, 0.15, r));
    const float halo = exp(-r*r*14.0)*0.22 + exp(-r*5.0)*0.05;
    const vec3  tint = textureLUT(tLUT, pos, push.sunDir).rgb;
    lum   = tint*(core*(disc*14.0) + vec3(1.0,0.85,0.6)*halo*2.0);
    clr.a = 1.0 - disc;
    } else {
    lum = textureEmmisive(clr.rgb);
    }

  lum *= push.GSunIntensity;
  lum *= scene.exposure;

  if(rayIntersect(pos, view, RPlanet)>=0.0) {
    lum   = vec3(0.0);
    clr.a = 1.0;
    }

  outColor = vec4(lum, clr.a);
  }
