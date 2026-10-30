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
  vec4 pos;   // player: x, y (feet), z, age 0..1
  vec4 misc;  // seed
  } push;

layout(location = 0) out vec4 outColor;

vec3 project(mat4 m, vec3 pos) {
  vec4 p = m*vec4(pos,1);
  return p.xyz/p.w;
  }

float hash11(float p) {
  p = fract(p*0.1031);
  p *= p + 33.33;
  p *= p + p;
  return fract(p);
  }

float vnoise(float x) {
  const float i = floor(x);
  const float f = fract(x);
  return mix(hash11(i), hash11(i + 1.0), f*f*(3.0 - 2.0*f)) - 0.5;
  }

// vertical jagged bolts around the player, blue-white, like the barrier striking
void main() {
  const ivec2 size = textureSize(depth,0);
  const vec2  ndc  = (vec2(gl_FragCoord.xy)/vec2(size))*2.0 - 1.0;
  const float dMax = texelFetch(depth, ivec2(gl_FragCoord.xy), 0).r;

  const vec3  p0     = project(scene.viewProjectInv, vec3(ndc,0.0));
  const vec3  p1     = project(scene.viewProjectInv, vec3(ndc,1.0));
  const vec3  dir    = normalize(p1 - p0);
  const vec3  org    = scene.camPos;
  const vec3  pz     = project(scene.viewProjectInv, vec3(ndc,dMax));
  const float tScene = dMax==1.0 ? 1.0e6 : length(pz - org);

  const float age  = push.pos.w;
  const float time = float(scene.tickCount32)*0.001;
  const float tick = floor(time*28.0); // the bolts flicker

  float v = 0.0;
  for(int i=0; i<4; ++i) {
    const float fi  = float(i);
    const float ang = push.misc.x + fi*1.5708 + (hash11(push.misc.x + fi*3.1) - 0.5)*0.9;
    const float rad = 30.0 + 45.0*hash11(push.misc.x + fi*7.7);
    const vec2  bxz = push.pos.xz + vec2(cos(ang), sin(ang))*rad;

    // where the ray passes the vertical axis of the bolt
    const vec2  h  = dir.xz;
    const float h2 = dot(h,h);
    if(h2<1.0e-6)
      continue;
    const float t0 = dot(bxz - org.xz, h)/h2;
    if(t0<0.0 || t0>tScene + 40.0)
      continue;
    const float y  = org.y + dir.y*t0;
    const float yl = (y - push.pos.y)/250.0;           // 0 at the feet
    if(yl<-0.05 || yl>1.15)
      continue;

    // the zig-zag of the bolt at this height
    const float seed = push.misc.x*13.0 + fi*17.0 + tick*0.37;
    const float k    = y*0.11;
    const vec2  jag  = vec2(vnoise(k + seed) *26.0 + vnoise(k*3.1 + seed*1.7)*12.0 + vnoise(k*8.0 + seed*2.3)*5.0,
                            vnoise(k + seed + 40.0)*26.0 + vnoise(k*3.1 + seed*1.3 + 9.0)*12.0 + vnoise(k*8.0 + seed*2.9)*5.0);
    const vec3  c    = vec3(bxz.x + jag.x, y, bxz.y + jag.y);
    const float d    = length(cross(c - org, dir));

    const float on   = step(0.3, hash11(tick*1.9 + fi*5.3));
    const float mask = smoothstep(-0.05, 0.05, yl)*(1.0 - smoothstep(1.0, 1.15, yl));
    v += on*mask*(exp(-d*d/(2.6*2.6)) + 0.30*exp(-d*d/(9.0*9.0)));
    }

  const float env = pow(clamp(1.0 - age, 0.0, 1.0), 0.6);
  v *= env;

  if(v<=0.001) {
    outColor = vec4(0);
    return;
    }
  const vec3 clr = vec3(0.25,0.45,1.0)*v + vec3(0.7,0.85,1.0)*(v*v*0.5);
  outColor = vec4(clr*(300000.0*scene.exposure), 0.0);
  }
