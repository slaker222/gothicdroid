#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive    : enable

#include "scene.glsl"
#include "common.glsl"

layout(binding = 0, std140) uniform UboScene {
  SceneDesc scene;
  };
layout(binding = 1) uniform sampler2D tBarrier;

layout(push_constant, std430) uniform UboPush {
  vec3  center;   // dome centre, world space
  float radius;
  vec4  strike;   // x: azimuth (turns), y: age 0..1 (<0 - none), z: seed, w: visibility 0..1
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

vec3 lightning(vec2 uv, float split) {
  // slight channel split gives the colour fringes of the original
  return vec3(textureLod(tBarrier, uv + vec2( split, 0), 0.7).r,
              textureLod(tBarrier, uv,                   0.7).g,
              textureLod(tBarrier, uv - vec2( split, 0), 0.7).b);
  }

float vnoise(float x) {
  const float i = floor(x);
  const float f = fract(x);
  return mix(hash11(i), hash11(i + 1.0), f*f*(3.0 - 2.0*f)) - 0.5;
  }

// sideways offset of a jagged bolt at the distance s along it
float jag(float s, float seed) {
  return vnoise(s*5.0 + seed)*0.10 + vnoise(s*13.0 + seed*1.7)*0.045 + vnoise(s*31.0 + seed*2.3)*0.018;
  }

// thin white bolt with branches, from the zenith towards the horizon; view - direction of the pixel
float strike(vec3 view) {
  const float age = push.strike.y;
  if(age<0.0)
    return 0.0;
  float dAz = atan(view.x, view.z)*(1.0/6.2831853) - push.strike.x;
  dAz -= floor(dAz + 0.5);
  const float theta = acos(clamp(view.y, -1.0, 1.0));
  const float side  = dAz*6.2831853*sin(theta);       // sideways distance, radians
  const float seed  = push.strike.z;

  float v = 0.0;
  for(int i=0; i<4; ++i) {
    // i==0 the main bolt, the rest are branches leaving it
    const float sb  = i==0 ? 0.25 : 0.45 + 0.25*float(i);
    const float dir = (hash11(seed + float(i)*7.1) - 0.5)*1.6;
    float x = jag(theta, seed);
    if(i>0)
      x += (theta - sb)*dir + jag(theta, seed + float(i)*3.3)*0.8;
    const float w    = i==0 ? 0.0045 : 0.0030;
    const float mask = smoothstep(sb, sb + 0.05, theta)*(1.0 - smoothstep(i==0 ? 1.45 : sb + 0.35, i==0 ? 1.55 : sb + 0.5, theta));
    const float d    = (side - x)/w;
    v += mask*(exp(-d*d) + 0.22*exp(-d*d*0.02))*(i==0 ? 1.0 : 0.6);
    }
  // two quick strikes, then it fades
  const float env = max(exp(-age*7.0), 0.9*exp(-(age - 0.2)*(age - 0.2)*500.0));
  return v*env;
  }

void main() {
  const vec2 ndc  = gl_FragCoord.xy*scene.screenResInv*2.0 - 1.0;
  const vec3 view = normalize(project(scene.viewProjectLwcInv, vec3(ndc,1.0)));

  // camera is inside the dome: far side of the sphere
  const vec3  o = scene.camPos - push.center;
  const float b = dot(o, view);
  const float c = dot(o, o) - push.radius*push.radius;
  const float h = b*b - c;
  if(h<=0.0 || c>0.0) {
    outColor = vec4(0,0,0,1);
    return;
    }
  const float t = -b + sqrt(h);
  const vec3  n = (o + view*t)/push.radius;
  if(n.y<=0.0) {
    outColor = vec4(0,0,0,1);
    return;
    }

  const float time  = float(scene.tickCount32)*0.001;
  const float az    = atan(n.x, n.z)*(1.0/6.2831853);   // -0.5..0.5
  const float theta = acos(clamp(n.y, -1.0, 1.0));      // 0 at the zenith

  // cylinder mapping near the horizon, planar mapping overhead (a cylinder pinches into a point at the zenith)
  const vec2  planar = n.xz/(n.y + 0.35);
  const float wTop = smoothstep(0.30, 0.75, n.y);

  const vec2 c0 = vec2(az*12.0 + time*0.010, theta*1.7 + time*0.050);
  const vec2 c1 = vec2(az* 8.0 - time*0.016, theta*1.3 - time*0.035 + 0.37);
  const vec2 c2 = vec2(az*17.0 + time*0.022, theta*2.2 - time*0.060 + 0.71);
  const vec2 f0 = planar*2.8 + vec2( time*0.010, time*0.050);
  const vec2 f1 = planar*1.9 + vec2(-time*0.016, time*0.035 + 0.37);
  const vec2 f2 = planar*3.9 + vec2( time*0.022,-time*0.060 + 0.71);

  vec3 lSide = lightning(c0, 0.004) + lightning(c1, 0.003)*0.8 + lightning(c2, 0.003)*0.5;
  vec3 lTop  = lightning(f0, 0.004) + lightning(f1, 0.003)*0.8 + lightning(f2, 0.003)*0.5;
  vec3 l     = mix(lSide, lTop, wTop);

  // the bolts flicker and flare up unevenly
  const float k0    = floor(time*3.0);
  const float flare = 0.75 + 0.25*sin(time*7.0 + az*40.0)
                    + 0.6*step(0.8, hash11(k0))*(1.0 - fract(time*3.0));
  l *= flare;
  // overhead the original barrier is faint
  l *= mix(1.0, 0.4, smoothstep(0.85, 0.99, n.y));

  // stronger near the wall, a weak glow in the middle of the valley
  const float edge   = length((scene.camPos - push.center).xz)/push.radius;
  const float near   = mix(0.3, 1.0, smoothstep(0.4, 0.95, edge));
  const float fade   = smoothstep(0.0, 0.08, n.y);

  l = l*l*(1.0+l);
  const float core = dot(l, vec3(0.33));
  const vec3 clr = l*vec3(0.60,0.65,1.00) + vec3(1.0,0.97,0.85)*core*core*core*1.6 + vec3(0.60,0.32,0.12)*0.10;
  // the barrier dims the sky behind it
  const vec3 bolt = vec3(0.85,0.90,1.0)*(strike(view)*3.0);
  const float vis = push.strike.w;
  outColor = vec4((clr*near + bolt)*fade*vis*210000.0*scene.exposure, 1.0 - 0.4*near*fade*vis);
  }
