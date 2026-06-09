CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c11 -lm -Icore -Icore/dsp

EMCC = emcc
EMCC_FLAGS = -O3 -Wall -s WASM=1 -s EXPORTED_RUNTIME_METHODS='["cwrap","HEAP8","HEAPU8","HEAPF32"]' -s EXPORTED_FUNCTIONS='["_wasm_init", "_wasm_reset", "_wasm_process", "_wasm_set_param", "_wasm_set_quality_profile", "_wasm_get_active_voice_limit", "_wasm_alloc", "_wasm_free", "_wasm_get_envelope", "_wasm_get_state", "_wasm_get_active_voices", "_wasm_get_peak_l", "_wasm_get_peak_r", "_wasm_get_clip_count", "_wasm_get_limiter_gain"]' -s ALLOW_MEMORY_GROWTH=1 -s NO_EXIT_RUNTIME=1 -s MODULARIZE=1 -s EXPORT_ES6=1 -s SINGLE_FILE=1 -s ENVIRONMENT='web,worker,worklet' -Icore -Icore/dsp

all: offline wasm macro-matrix

offline: platform/offline/sound_bubbles_render

platform/offline/sound_bubbles_render: platform/offline/sound_bubbles_render.c core/engine/bubble_engine.c core/dsp/sound_bubbles_dsp.c
	$(CC) $^ $(CFLAGS) -o $@

wasm: web/frontend/bubble_cloud_wasm.js

macro-matrix: web/frontend/macro_matrix.js

web/frontend/macro_matrix.js: docs/macro_matrix.yaml scripts/generate_macro_matrix_js.py
	python3 scripts/generate_macro_matrix_js.py --source docs/macro_matrix.yaml --output web/frontend/macro_matrix.js

web/frontend/bubble_cloud_wasm.js: web/wasm/bubble_cloud_wasm.c core/engine/bubble_engine.c core/dsp/sound_bubbles_dsp.c
	$(EMCC) $^ $(EMCC_FLAGS) -o $@

clean:
	rm -f platform/offline/sound_bubbles_render web/frontend/bubble_cloud_wasm.js web/frontend/macro_matrix.js

.PHONY: all offline wasm macro-matrix clean
