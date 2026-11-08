#pragma once

// phase heartbeats for wasm world-build diagnostics; writes bypass the console
// pipeline into window.__hb (latest phase + ring-buffer trace); no-op elsewhere.
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <unistd.h>
#include <stdint.h>

// memory of the load: a [LOADMEM] line every time the top of the heap (sbrk) grew by 24 MB, with the phase it is in, sent at once
// (window.__ogLogBeacon of the ?log page: the main thread is busy while the world loads, an out of memory crash of the
// tab would lose what was not sent)
#define OG_HB_VALS(PHASE, A, B) EM_ASM_({ \
    var h = window.__hb || (window.__hb = {trace: [], seq: 0}); \
    if (!h.trace) { h.trace = []; h.seq = 0; } \
    var now = Date.now(); \
    h.phase = PHASE; h.val0 = $0; h.val1 = $1; h.t = now; h.seq++; \
    h.trace.push([h.seq, now, PHASE, $0, $1]); \
    if (h.trace.length > 4000) h.trace.splice(0, 1000); \
    var used = $2 | 0; var heap = (HEAP8.length / 1048576) | 0; \
    if (!h.memLast || used >= h.memLast + 24 || heap > (h.heapLast|0) + 64) { \
      h.memLast = used; h.heapLast = heap; \
      console.log('[LOADMEM] memory sbrk=' + used + 'M heap=' + heap + 'M phase=' + PHASE + ' ' + $0 + ' ' + $1); \
      if (window.__ogLogBeacon) window.__ogLogBeacon(); \
    } \
    }, (int)(A), (int)(B), (int)(uintptr_t(sbrk(0))>>20))

#define OG_HB_STR(PHASE, STR) EM_ASM_({ \
    var h = window.__hb || (window.__hb = {trace: [], seq: 0}); \
    if (!h.trace) { h.trace = []; h.seq = 0; } \
    var now = Date.now(); var s = UTF8ToString($0); \
    h.phase = PHASE; h.str = s; h.t = now; h.seq++; \
    h.trace.push([h.seq, now, PHASE, s]); \
    if (h.trace.length > 4000) h.trace.splice(0, 1000); \
    var used = $2 | 0; var heap = (HEAP8.length / 1048576) | 0; \
    if (!h.memLast || used >= h.memLast + 24 || heap > (h.heapLast|0) + 64) { \
      h.memLast = used; h.heapLast = heap; \
      console.log('[LOADMEM] memory sbrk=' + used + 'M heap=' + heap + 'M phase=' + PHASE + ' ' + s); \
      if (window.__ogLogBeacon) window.__ogLogBeacon(); \
    } \
    }, (STR), 0, (int)(uintptr_t(sbrk(0))>>20))

#define OG_HB(PHASE) OG_HB_VALS(PHASE, 0, 0)
#else
#define OG_HB_STR(PHASE, STR) ((void)0)
#define OG_HB(PHASE) ((void)0)
#define OG_HB_VALS(PHASE, A, B) ((void)0)
#endif
