CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c11 -lm -Icore

EMCC = emcc
EMCC_FLAGS = -O3 -Wall -s WASM=1 -s EXPORTED_RUNTIME_METHODS='["cwrap","HEAP8","HEAPU8","HEAPF32"]' -s EXPORTED_FUNCTIONS='["_wasm_init", "_wasm_reset", "_wasm_process", "_wasm_set_param", "_wasm_alloc", "_wasm_free", "_wasm_get_envelope", "_wasm_get_state", "_wasm_get_active_voices"]' -s ALLOW_MEMORY_GROWTH=1 -s NO_EXIT_RUNTIME=1 -s MODULARIZE=1 -s EXPORT_ES6=1 -s SINGLE_FILE=0 -s ENVIRONMENT='web,worker,worklet' -Icore

all: offline wasm

offline: platform/offline/sound_bubbles_render

platform/offline/sound_bubbles_render: platform/offline/sound_bubbles_render.c core/sound_bubbles_dsp.c
	$(CC) $^ $(CFLAGS) -o $@

wasm: web/frontend/bubble_cloud_wasm.js

web/frontend/bubble_cloud_wasm.js: web/wasm/bubble_cloud_wasm.c core/sound_bubbles_dsp.c
	$(EMCC) $^ $(EMCC_FLAGS) -o $@

clean:
	rm -f platform/offline/sound_bubbles_render web/frontend/bubble_cloud_wasm.js web/frontend/bubble_cloud_wasm.wasm

.PHONY: all offline wasm clean
