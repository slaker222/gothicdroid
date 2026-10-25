#pragma once

// phase heartbeats for wasm world-build diagnostics; writes bypass the console
// pipeline into window.__hb (latest phase + ring-buffer trace); no-op elsewhere.
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

#define OG_HB_VALS(PHASE, A, B) EM_ASM_({ \
    var h = window.__hb || (window.__hb = {trace: [], seq: 0}); \
    if (!h.trace) { h.trace = []; h.seq = 0; } \
    var now = Date.now(); \
    h.phase = PHASE; h.val0 = $0; h.val1 = $1; h.t = now; h.seq++; \
    h.trace.push([h.seq, now, PHASE, $0, $1]); \
    if (h.trace.length > 4000) h.trace.splice(0, 1000); \
    }, (int)(A), (int)(B))

#define OG_HB_STR(PHASE, STR) EM_ASM_({ \
    var h = window.__hb || (window.__hb = {trace: [], seq: 0}); \
    if (!h.trace) { h.trace = []; h.seq = 0; } \
    var now = Date.now(); var s = UTF8ToString($0); \
    h.phase = PHASE; h.str = s; h.t = now; h.seq++; \
    h.trace.push([h.seq, now, PHASE, s]); \
    if (h.trace.length > 4000) h.trace.splice(0, 1000); \
    }, (STR))

#define OG_HB(PHASE) OG_HB_VALS(PHASE, 0, 0)
#else
#define OG_HB_STR(PHASE, STR) ((void)0)
#define OG_HB(PHASE) ((void)0)
#define OG_HB_VALS(PHASE, A, B) ((void)0)
#endif
