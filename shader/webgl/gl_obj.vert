#version 450

// WebGL 2 compatibility renderer: plain vertex buffers, textures only (see GlScene)

layout(push_constant, std430) uniform Push {
  mat4 mvp;
  mat4 model;
  } push;

#if defined(SKINING)
layout(binding = 1, std140) uniform Bones {
  mat4 bones[128];
  } anim;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUv;
layout(location = 2) in uint inColor;
layout(location = 3) in vec3 inPos0;
layout(location = 4) in vec3 inPos1;
layout(location = 5) in vec3 inPos2;
layout(location = 6) in vec3 inPos3;
layout(location = 7) in uint inBoneId;
layout(location = 8) in vec4 inWeight;
#else
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in uint inColor;
#endif

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec3 outNormal;
// not used by the fragment shader: keeps the input in the module, so the vertex layout has no hole
layout(location = 2) out vec4 outColor;

void main() {
  outUv    = inUv;
  outColor = vec4((uvec4(inColor) >> uvec4(0u,8u,16u,24u)) & 0xFFu)/255.0; // no unpackUnorm4x8 in GLSL ES 3.00
#if defined(SKINING)
  const uvec4 id = uvec4(inBoneId & 0xFFu, (inBoneId >> 8) & 0xFFu, (inBoneId >> 16) & 0xFFu, (inBoneId >> 24) & 0xFFu);
  const mat4  b0 = anim.bones[id.x];
  const mat4  b1 = anim.bones[id.y];
  const mat4  b2 = anim.bones[id.z];
  const mat4  b3 = anim.bones[id.w];
  const vec3  pos = (b0*vec4(inPos0,1.0)).xyz*inWeight.x +
                    (b1*vec4(inPos1,1.0)).xyz*inWeight.y +
                    (b2*vec4(inPos2,1.0)).xyz*inWeight.z +
                    (b3*vec4(inPos3,1.0)).xyz*inWeight.w;
  outNormal   = mat3(b0)*inNormal;
  gl_Position = push.mvp*vec4(pos,1.0);
#else
  outNormal   = mat3(push.model)*inNormal;
  gl_Position = push.mvp*vec4(inPos,1.0);
#endif
  }
