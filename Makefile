CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c11 -lm -Icore -Icore/dsp

EMCC = emcc
EMCC_FLAGS = -O3 -Wall -s WASM=1 -s EXPORTED_RUNTIME_METHODS='["cwrap","HEAP8","HEAPU8","HEAPF32"]' -s EXPORTED_FUNCTIONS='["_wasm_init", "_wasm_reset", "_wasm_process", "_wasm_set_param", "_wasm_set_quality_profile", "_wasm_get_active_voice_limit", "_wasm_alloc", "_wasm_free", "_wasm_get_envelope", "_wasm_get_state", "_wasm_get_active_voices", "_wasm_get_peak_l", "_wasm_get_peak_r", "_wasm_get_clip_count", "_wasm_get_limiter_gain"]' -s ALLOW_MEMORY_GROWTH=1 -s NO_EXIT_RUNTIME=1 -s MODULARIZE=1 -s EXPORT_ES6=1 -s SINGLE_FILE=1 -s ENVIRONMENT='web,worker,worklet' -Icore -Icore/dsp

PRESET_HEADERS = core/presets/bubble_preset.h core/schema/bubble_preset_schema.h
OFFLINE_SRCS = platform/offline/sound_bubbles_render.c core/presets/bubble_preset.c core/engine/bubble_engine.c core/dsp/sound_bubbles_dsp.c
WASM_SRCS = web/wasm/bubble_cloud_wasm.c core/presets/bubble_preset.c core/engine/bubble_engine.c core/dsp/sound_bubbles_dsp.c

all: offline wasm

offline: platform/offline/sound_bubbles_render

platform/offline/sound_bubbles_render: $(OFFLINE_SRCS) $(PRESET_HEADERS)
	$(CC) $(OFFLINE_SRCS) $(CFLAGS) -o $@

wasm: web/frontend/bubble_cloud_wasm.js

web/frontend/bubble_cloud_wasm.js: $(WASM_SRCS) $(PRESET_HEADERS)
	$(EMCC) $(WASM_SRCS) $(EMCC_FLAGS) -o $@

clean:
	rm -f platform/offline/sound_bubbles_render web/frontend/bubble_cloud_wasm.js

.PHONY: all offline wasm clean
