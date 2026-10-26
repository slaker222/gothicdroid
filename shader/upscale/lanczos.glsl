#ifndef LANCZOS_GLSL
#define LANCZOS_GLSL

float lanczosWeight(float x, float r) {
  const float PI_SQ = 9.8696044010893586188344910;
  if(x == 0.0)
    return 1.;
  return (r * sin(M_PI * x) * sin(M_PI * (x / r) )) / (PI_SQ * x*x);
  }

float lanczosWeight(vec2 x, float r) {
  return lanczosWeight(x.x, r) * lanczosWeight(x.y, r);
  }

vec3 lanczosUpscale(in sampler2D img, vec2 coord) {
  const int r = 2;

  vec2 res    = vec2(textureSize(img, 0));
  vec2 resInv = 1.0 / res;
  coord      += -0.5 * resInv;
  vec2 ccoord = floor(coord * res) * resInv;

  vec3 total = vec3(0);
  [[unroll]]
  for(int x = -r; x <= r; x++) {
    [[unroll]]
    for(int y = -r; y <= r; y++) {
      if(abs(x)==r && abs(y)==r)
        continue;

      vec2  offs   = vec2(x,y);
      vec2  sco    = offs * resInv + ccoord;
      vec2  d      = ((sco - coord) * res);

      vec3  val    = textureLod(img, sco+0.5*resInv, 0.0).rgb;
      float weight = lanczosWeight(d, float(r));

      total     += val * weight;
      }
    }

  return total;
  }

// The same filter for a bilinear sampler: along an axis the weights of the taps (-2,-1) and (0,1)
// have one sign each, so every pair is one bilinear tap placed between its two texels
// (exact), 3x3 taps instead of 21. The four corner taps, which lanczosUpscale leaves out,
// come along with their weights (each below 0.1% of the sum).
vec3 lanczosUpscaleLinear(in sampler2D img, vec2 coord) {
  const int r = 2;

  vec2 res    = vec2(textureSize(img, 0));
  vec2 resInv = 1.0 / res;
  coord      += -0.5 * resInv;
  vec2 cres   = coord * res;
  vec2 cfloor = floor(cres);
  vec2 fr     = cres - cfloor;

  float wx[2*r+1];
  float wy[2*r+1];
  [[unroll]]
  for(int i = 0; i <= 2*r; i++) {
    wx[i] = lanczosWeight(float(i-r) - fr.x, float(r));
    wy[i] = lanczosWeight(float(i-r) - fr.y, float(r));
    }

  // taps: texels -2..-1, 0..1 and 2 of the axis
  float gx[3] = float[3](wx[0]+wx[1], wx[2]+wx[3], wx[4]);
  float gy[3] = float[3](wy[0]+wy[1], wy[2]+wy[3], wy[4]);
  float px[3] = float[3](-2.0 + (abs(gx[0])>1e-12 ? wx[1]/gx[0] : 0.5),
                          0.0 + (abs(gx[1])>1e-12 ? wx[3]/gx[1] : 0.5),
                          2.0);
  float py[3] = float[3](-2.0 + (abs(gy[0])>1e-12 ? wy[1]/gy[0] : 0.5),
                          0.0 + (abs(gy[1])>1e-12 ? wy[3]/gy[1] : 0.5),
                          2.0);

  vec2 base  = (cfloor + 0.5) * resInv;
  vec3 total = vec3(0);
  [[unroll]]
  for(int x = 0; x < 3; x++) {
    [[unroll]]
    for(int y = 0; y < 3; y++) {
      vec3 val = textureLod(img, base + vec2(px[x], py[y])*resInv, 0.0).rgb;
      total   += val * (gx[x]*gy[y]);
      }
    }
  return total;
  }

#endif
