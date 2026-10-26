// memcmp for the web build: musl's version compares one byte per step, and
// memcmp is hot here (render pass call logs, matrix equality checks).
// Same result: the first differing byte decides, compared as unsigned char.
#include <stddef.h>
#include <wasm_simd128.h>

int memcmp(const void* vl, const void* vr, size_t n) {
  const unsigned char* l = (const unsigned char*)vl;
  const unsigned char* r = (const unsigned char*)vr;
  while(n>=16) {
    if(!wasm_i8x16_all_true(wasm_i8x16_eq(wasm_v128_load(l), wasm_v128_load(r))))
      break;
    l += 16;
    r += 16;
    n -= 16;
    }
  for(; n>0; --n, ++l, ++r)
    if(*l!=*r)
      return *l - *r;
  return 0;
  }
