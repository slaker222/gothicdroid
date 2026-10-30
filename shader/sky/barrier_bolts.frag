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
  for(int i=0; i<6; ++i) {
    const float fi  = float(i);
    // every few frames a bolt jumps to a new place around the body
    const float rnd = floor(tick/3.0 + fi*1.7);
    const float h0  = hash11(push.misc.x + fi*3.1 + rnd*11.3);
    const float h1  = hash11(push.misc.x + fi*7.7 + rnd*5.9);
    const float ang = h0*6.2832;
    const float rad = 28.0 + 40.0*h1;

    const float on = step(0.25, hash11(tick*1.9 + fi*5.3));
    const float seed = push.misc.x*13.0 + fi*17.0 + rnd*3.7;
    const float spin = (h1 - 0.5)*0.012;

    // the bolt is nearly vertical: find where the ray passes its axis (iterated, as the axis moves with height)
    const vec2  h  = dir.xz;
    const float h2 = dot(h,h);
    if(h2<1.0e-6)
      continue;
    float t0 = dot(push.pos.xz - org.xz, h)/h2;
    for(int it=0; it<2; ++it) {
      const float y  = org.y + dir.y*t0;
      const float k  = y*0.09;
      // swirls round the body and bends toward / away from the character
      const float a  = ang + (y - push.pos.y)*spin + vnoise(k*0.5 + seed)*3.0;
      const float r  = rad*(0.55 + 1.1*(vnoise(k*0.6 + seed + 7.0) + 0.5));
      const vec2  bxz = push.pos.xz + vec2(cos(a), sin(a))*r;
      t0 = dot(bxz - org.xz, h)/h2;
      }
    if(t0<0.0 || t0>tScene + 40.0)
      continue;
    const float y  = org.y + dir.y*t0;
    const float yl = (y - push.pos.y)/220.0;           // 0 at the feet
    if(yl<-0.05 || yl>1.2)
      continue;

    const float k  = y*0.09;
    const float a  = ang + (y - push.pos.y)*spin + vnoise(k*0.5 + seed)*3.0;
    const float r  = rad*(0.55 + 1.1*(vnoise(k*0.6 + seed + 7.0) + 0.5));
    // sharp small zig-zags on top of the big bends
    const vec2  jag = vec2(vnoise(k*3.0 + seed*1.7)*14.0 + vnoise(k*9.0 + seed*2.3)*7.0 + vnoise(k*23.0 + seed)*3.0,
                           vnoise(k*3.0 + seed*1.3 + 9.0)*14.0 + vnoise(k*9.0 + seed*2.9)*7.0 + vnoise(k*23.0 + seed*1.1)*3.0);
    const vec3  c   = vec3(push.pos.x + cos(a)*r + jag.x, y, push.pos.z + sin(a)*r + jag.y);
    const float d   = length(cross(c - org, dir));

    const float mask = smoothstep(-0.05, 0.05, yl)*(1.0 - smoothstep(1.0, 1.2, yl));
    v += on*mask*(exp(-d*d/(2.0*2.0)) + 0.30*exp(-d*d/(8.0*8.0)));
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
