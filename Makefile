EMCC ?= emcc
CFLAGS = -O3 -ffast-math

WASM_OUT = web/frontend/bubble_cloud_wasm.js
WASM_SRC = sound_bubbles_dsp.c wasm_bridge.c

.PHONY: wasm clean

wasm:
	$(EMCC) $(WASM_SRC) $(CFLAGS) \
	  -s WASM=1 \
	  -s SINGLE_FILE=1 \
	  -s MODULARIZE=1 \
	  -s ENVIRONMENT='web,worker,worklet' \
	  -s ALLOW_MEMORY_GROWTH=1 \
	  -s EXPORTED_FUNCTIONS='["_wasm_init","_wasm_reset","_wasm_process","_wasm_set_param","_wasm_alloc","_wasm_free","_wasm_get_envelope","_wasm_get_state","_wasm_get_active_voices"]' \
	  -s EXPORTED_RUNTIME_METHODS='["cwrap","HEAPF32"]' \
	  -o $(WASM_OUT)

clean:
	rm -f $(WASM_OUT)
