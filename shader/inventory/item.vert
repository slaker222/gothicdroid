#version 450
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex {
  vec4 gl_Position;
  };
layout(location = 0) out vec2 UV;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in uint inColor;

layout(push_constant, std430) uniform UboPush {
  mat4 viewProject;
  };

void main() {
  UV          = inUV;
  // the vertex layout is built from the inputs the shader uses (ShaderReflection::getVertexDecl): an unused
  // normal or colour drops out and the UV would be read from the normal's place. Never true (normals are unit
  // length), the compiler cannot remove it
  if(inColor==0xFFFFFFFEu && inNormal.x>1e9)
    UV = vec2(0);
  gl_Position = viewProject*vec4(inPos,1.0);
  }
