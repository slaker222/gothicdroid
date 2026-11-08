#version 450

// WebGL 2 renderer: the sky of the original game. Layer 0 is the sky itself (opaque), layer 1 the clouds over it;
// at night the stars and the night clouds. Both are planes high above the camera that drift with time;
// towards the horizon the sky goes into its own average colour.

layout(push_constant, std430) uniform Push {
  mat4 invViewProj; // camera at the origin
  vec4 offset;      // xy: layer 0, zw: layer 1
  vec4 params;      // x: night phase 0..1, y: brightness
  } push;

layout(binding = 0) uniform sampler2D dayL0;
layout(binding = 1) uniform sampler2D dayL1;
layout(binding = 2) uniform sampler2D nightL0;
layout(binding = 3) uniform sampler2D nightL1;

layout(location = 0) in  vec2 UV;
layout(location = 0) out vec4 outColor;

vec4 layers(sampler2D l0, sampler2D l1, vec2 uv, float k) {
  vec3 base   = texture(l0, uv*0.5 + push.offset.xy).rgb;
  vec4 cloud  = texture(l1, uv     + push.offset.zw);
  vec3 c      = mix(base, cloud.rgb, cloud.a);
  vec3 hor    = textureLod(l0, vec2(0.5), 16.0).rgb;   // the average of the sky
  return vec4(mix(hor, c, k), 1.0);
  }

void main() {
  const vec2 ndc  = UV*2.0 - vec2(1.0);
  vec4  pn  = push.invViewProj*vec4(ndc, 0.0, 1.0);
  vec4  pf  = push.invViewProj*vec4(ndc, 1.0, 1.0);
  vec3  dir = normalize(pf.xyz/pf.w - pn.xyz/pn.w);

  // the plane of the sky: flat, so the clouds get smaller towards the horizon
  const float h  = max(dir.y, 0.02);
  const vec2  uv = dir.xz/h*0.25;
  const float k  = smoothstep(0.0, 0.25, dir.y);       // horizon blend

  vec3 day   = layers(dayL0,   dayL1,   uv, k).rgb;
  vec3 night = layers(nightL0, nightL1, uv, k).rgb;
  vec3 c     = mix(day, night, push.params.x)*push.params.y;
  outColor = vec4(c, 1.0);
  }
