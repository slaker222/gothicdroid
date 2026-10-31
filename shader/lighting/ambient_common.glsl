#ifndef AMBIENT_COMMON_GLSL
#define AMBIENT_COMMON_GLSL

// needs: scene, gbufNormal (usampler2D), irradiance (texture2D), ssao (sampler2D, with SSAO);
// shared by ambient_light.frag and by direct_light.frag (AMBIENT: both lights in one pass, the g-buffer is read once)

#if defined(SSAO)
float textureSsao() { return textureLod(ssao, gl_FragCoord.xy*scene.screenResInv, 0).r; }
#else
float textureSsao() { return 1; }
#endif

vec3 skyIrradiance() {
  vec3 n = texelFetch(gbufNormal, ivec2(gl_FragCoord.xy), 0).rgb;
  n = normalize(n*2.0 - vec3(1.0));

  ivec3 d;
  d.x = n.x>=0 ? 1 : 0;
  d.y = n.y>=0 ? 1 : 0;
  d.z = n.z>=0 ? 1 : 0;

  n = n*n;

  vec3 ret = vec3(0);
  ret += texelFetch(irradiance, ivec2(0,d.x), 0).rgb * n.x;
  ret += texelFetch(irradiance, ivec2(1,d.y), 0).rgb * n.y;
  ret += texelFetch(irradiance, ivec2(2,d.z), 0).rgb * n.z;

  return ret;
  }

// the ambient light of a pixel (the standard, not the simple lighting)
vec3 ambientColor(const vec3 linear, const vec3 norm) {
  const float ao = textureSsao();

  vec3 ambient = scene.ambient + (norm.y*0.25+0.75) * NightAmbient * Fd_Lambert;
  vec3 sky     = skyIrradiance(); // * Fd_Lambert is accounted in integration

  vec3 luminance  = sky + ambient;

  vec3 color = luminance;
  color *= linear;
  color *= ao;
  color *= scene.exposure;
  return color;
  }

#endif
