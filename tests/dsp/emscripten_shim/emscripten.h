#ifndef EMSCRIPTEN_NATIVE_SHIM_H
#define EMSCRIPTEN_NATIVE_SHIM_H

/*
 * Minimal native shim so platform/wasm/bubble_cloud_wasm.c can be compiled and
 * exercised by a normal host compiler during parity testing. It intentionally
 * provides only the macro the WASM source uses.
 */
#ifndef EMSCRIPTEN_KEEPALIVE
#define EMSCRIPTEN_KEEPALIVE
#endif

#endif
