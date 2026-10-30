#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive    : enable

#include "scene.glsl"
#include "common.glsl"

layout(binding = 0) uniform sampler2D depth;
layout(binding = 1, std140) uniform UboScene {
  SceneDesc scene;
  };

layout(push_constant, std430) uniform UboPush {
  vec4 a;     // corners 0 and 1: x, z, x, z
  vec4 b;     // corners 2 and 3
  vec2 yRange;
  } push;

layout(location = 0) out vec4 outColor;

vec3 project(mat4 m, vec3 pos) {
  vec4 p = m*vec4(pos,1);
  return p.xyz/p.w;
  }

// translucent red box (a column of any height) of a lethal barrier zone, hidden behind the scene
void main() {
  const ivec2 size = textureSize(depth,0);
  const vec2  ndc  = (vec2(gl_FragCoord.xy)/vec2(size))*2.0 - 1.0;
  const float dMax = texelFetch(depth, ivec2(gl_FragCoord.xy), 0).r;

  const vec3 p0   = project(scene.viewProjectInv, vec3(ndc,0.0));
  const vec3 p1   = project(scene.viewProjectInv, vec3(ndc,1.0));
  const vec3 dir  = normalize(p1 - p0);
  const vec3 org  = scene.camPos;
  const vec3 pz   = project(scene.viewProjectInv, vec3(ndc,dMax));
  const float tScene = dMax==1.0 ? 1.0e6 : length(pz - org);

  // convex prism: every side is a half-space, the ray is clipped by them
  const vec2 c[4] = vec2[4](push.a.xy, push.a.zw, push.b.xy, push.b.zw);
  float area = 0.0;
  for(int i=0; i<4; ++i) {
    const vec2 p = c[i], q = c[(i+1)&3];
    area += p.x*q.y - q.x*p.y;
    }
  const float orient = area>=0.0 ? 1.0 : -1.0;

  float tNear = -1.0e30;
  float tFar  =  1.0e30;
  bool  miss  = false;
  for(int i=0; i<4; ++i) {
    const vec2 e = c[(i+1)&3] - c[i];
    if(dot(e,e)<1.0)
      continue; // a repeated corner of a triangle
    const vec2  n = orient*vec2(-e.y, e.x);
    const float d = dot(n, org.xz - c[i]);
    const float s = dot(n, dir.xz);
    if(abs(s)<1.0e-6) {
      miss = miss || d<0.0;
      } else if(s>0.0) {
      tNear = max(tNear, -d/s);
      } else {
      tFar  = min(tFar,  -d/s);
      }
    }
  // height
  if(abs(dir.y)<1.0e-6) {
    miss = miss || org.y<push.yRange.x || org.y>push.yRange.y;
    } else {
    const float ya = (push.yRange.x - org.y)/dir.y;
    const float yb = (push.yRange.y - org.y)/dir.y;
    tNear = max(tNear, min(ya,yb));
    tFar  = min(tFar,  max(ya,yb));
    }
  if(miss) {
    outColor = vec4(0);
    return;
    }

  const float t0 = max(tNear, 0.0);
  const float t1 = min(tFar,  tScene);
  if(t1<=t0) {
    outColor = vec4(0);
    return;
    }

  // fills with the path inside, the side wall that the ray enters through is brighter
  const float len  = t1 - t0;
  float a = 1.0 - exp(-len*0.00012);
  if(tNear>0.0 && tNear<tScene)
    a = max(a, 0.22);
  a = min(a, 0.4);

  const vec3 red = vec3(1.0, 0.08, 0.04);
  outColor = vec4(red*a*(30000.0*scene.exposure), a);
  }
