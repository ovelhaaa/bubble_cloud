#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "sound_bubbles_dsp.h"

// --- 1. Design Summary ---
// This file is a minimal, self-contained CLI renderer for the Sound Bubbles DSP core.
// It loads an input WAV file, a JSON preset, applies the DSP effect, and writes an output WAV file.
// All audio data is read into memory at once to keep the implementation simple.

// --- 2. WAV Format Assumptions ---
// Input: 44100 Hz, 1 or 2 channels, 16-bit PCM or 32-bit IEEE Float.
// Output: 44100 Hz, 2 channels, 32-bit IEEE Float.

// --- 3. Preset JSON Assumptions ---
// The JSON file must be a flat object containing numeric values.
// Keys are matched via simple string searching (`strstr`). Missing keys will leave default values.

// --- 4. CLI Behavior ---
// Usage: ./sound_bubbles_render input.wav preset.json output.wav

// --- 5. Error Handling Policy ---
// The program will print a descriptive error to stderr and return a non-zero exit code
// for invalid arguments, file I/O errors, unsupported WAV formats, or memory allocation failures.

// --- WAV Header Structures ---

#pragma pack(push, 1)
typedef struct {
    char chunkId[4];       // "RIFF"
    uint32_t chunkSize;
    char format[4];        // "WAVE"
    char subchunk1Id[4];   // "fmt "
    uint32_t subchunk1Size;
    uint16_t audioFormat;  // 1 = PCM, 3 = IEEE Float
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
} WavHeader;
#pragma pack(pop)

typedef struct {
    float* data;
    uint32_t numSamples;
    uint16_t numChannels;
    uint32_t sampleRate;
} WavData;

// --- Helper Functions ---

static void PrintUsage(const char* progName) {
    fprintf(stderr, "Usage: %s <input.wav> <preset.json> <output.wav>\n", progName);
}

static void ErrorExit(const char* message) {
    fprintf(stderr, "Error: %s\n", message);
    exit(EXIT_FAILURE);
}

// Minimal manual JSON parser for flat numeric keys
static float GetJsonFloat(const char* json_str, const char* key, float default_val) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);

    const char* pos = strstr(json_str, search_key);
    if (!pos) return default_val;

    pos = strchr(pos, ':');
    if (!pos) return default_val;

    pos++; // Skip ':'

    float val = default_val;
    if (sscanf(pos, " %f", &val) == 1) {
        return val;
    }
    return default_val;
}

static int32_t GetJsonInt(const char* json_str, const char* key, int32_t default_val) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);

    const char* pos = strstr(json_str, search_key);
    if (!pos) return default_val;

    pos = strchr(pos, ':');
    if (!pos) return default_val;

    pos++; // Skip ':'

    int32_t val = default_val;
    if (sscanf(pos, " %d", &val) == 1) {
        return val;
    }
    return default_val;
}

// --- WAV I/O ---

static WavData ReadWav(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) ErrorExit("Could not open input WAV file.");

    WavHeader header;
    if (fread(&header, sizeof(WavHeader), 1, file) != 1) {
        fclose(file);
        ErrorExit("Failed to read WAV header.");
    }

    if (strncmp(header.chunkId, "RIFF", 4) != 0 || strncmp(header.format, "WAVE", 4) != 0) {
        fclose(file);
        ErrorExit("Input file is not a valid WAV file.");
    }

    if (header.sampleRate != BUBBLES_SAMPLE_RATE) {
        fclose(file);
        ErrorExit("Input WAV must be 44.1 kHz.");
    }

    if (header.numChannels != 1 && header.numChannels != 2) {
        fclose(file);
        ErrorExit("Input WAV must be Mono or Stereo.");
    }

    if (header.audioFormat != 1 && header.audioFormat != 3) {
        fclose(file);
        ErrorExit("Unsupported audio format (must be PCM or IEEE Float).");
    }

    // Skip to data chunk
    char chunkId[4];
    uint32_t chunkSize;
    bool dataFound = false;

    // We might have an extended fmt chunk, cbSize etc. Let's just search for 'data' chunk
    // But safely, by reading chunks
    fseek(file, 12, SEEK_SET); // After RIFF... WAVE

    while (fread(chunkId, 1, 4, file) == 4) {
        if (fread(&chunkSize, 4, 1, file) != 1) break;
        if (strncmp(chunkId, "data", 4) == 0) {
            dataFound = true;
            break;
        }
        fseek(file, chunkSize, SEEK_CUR);
    }

    if (!dataFound) {
        fclose(file);
        ErrorExit("Could not find data chunk in WAV file.");
    }

    uint32_t numSamplesTotal = chunkSize / (header.bitsPerSample / 8);
    uint32_t numFrames = numSamplesTotal / header.numChannels;

    WavData wav;
    wav.numSamples = numFrames; // We'll store it as frames
    wav.numChannels = header.numChannels;
    wav.sampleRate = header.sampleRate;
    wav.data = (float*)malloc(numFrames * wav.numChannels * sizeof(float));

    if (!wav.data) {
        fclose(file);
        ErrorExit("Failed to allocate memory for input WAV data.");
    }

    if (header.audioFormat == 1 && header.bitsPerSample == 16) {
        int16_t* pcmData = (int16_t*)malloc(chunkSize);
        if (!pcmData || fread(pcmData, 1, chunkSize, file) != chunkSize) {
            free(pcmData);
            free(wav.data);
            fclose(file);
            ErrorExit("Failed to read PCM data.");
        }
        for (uint32_t i = 0; i < numSamplesTotal; i++) {
            wav.data[i] = (float)pcmData[i] / 32768.0f;
        }
        free(pcmData);
    } else if (header.audioFormat == 3 && header.bitsPerSample == 32) {
        if (fread(wav.data, 1, chunkSize, file) != chunkSize) {
            free(wav.data);
            fclose(file);
            ErrorExit("Failed to read Float data.");
        }
    } else {
        free(wav.data);
        fclose(file);
        ErrorExit("Unsupported bit depth (must be 16-bit PCM or 32-bit Float).");
    }

    fclose(file);
    return wav;
}

static void WriteWav(const char* filename, const float* left, const float* right, uint32_t numFrames) {
    FILE* file = fopen(filename, "wb");
    if (!file) ErrorExit("Could not open output WAV file for writing.");

    uint32_t bytesPerSample = 4; // 32-bit float
    uint32_t numChannels = 2;    // Stereo
    uint32_t byteRate = BUBBLES_SAMPLE_RATE * numChannels * bytesPerSample;
    uint32_t blockAlign = numChannels * bytesPerSample;
    uint32_t dataSize = numFrames * blockAlign;

    WavHeader header = {
        {'R', 'I', 'F', 'F'},
        36 + dataSize,
        {'W', 'A', 'V', 'E'},
        {'f', 'm', 't', ' '},
        16, // PCM/Float format chunk size
        3,  // IEEE Float
        (uint16_t)numChannels,
        BUBBLES_SAMPLE_RATE,
        byteRate,
        (uint16_t)blockAlign,
        (uint16_t)(bytesPerSample * 8)
    };

    fwrite(&header, sizeof(WavHeader), 1, file);

    const char dataChunkId[4] = {'d', 'a', 't', 'a'};
    fwrite(dataChunkId, 1, 4, file);
    fwrite(&dataSize, 4, 1, file);

    // Interleave float data
    float* interleaved = (float*)malloc(numFrames * 2 * sizeof(float));
    if (!interleaved) {
        fclose(file);
        ErrorExit("Failed to allocate memory for output interleaving.");
    }

    for (uint32_t i = 0; i < numFrames; i++) {
        interleaved[i * 2] = left[i];
        interleaved[i * 2 + 1] = right[i];
    }

    fwrite(interleaved, sizeof(float), numFrames * 2, file);

    free(interleaved);
    fclose(file);
}

// --- Parsing ---

static void LoadPreset(const char* filename, EngineConfig_t* config, float* master_dry, float* master_wet) {
    FILE* file = fopen(filename, "rb");
    if (!file) ErrorExit("Could not open JSON preset file.");

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* json_str = (char*)malloc(length + 1);
    if (!json_str) {
        fclose(file);
        ErrorExit("Failed to allocate memory for JSON parsing.");
    }

    if (fread(json_str, 1, length, file) != (size_t)length) {
        free(json_str);
        fclose(file);
        ErrorExit("Failed to read JSON preset file.");
    }
    json_str[length] = '\0';
    fclose(file);

    // Parse core parameters
    config->noise_floor = GetJsonFloat(json_str, "noise_floor", 0.001f);
    config->tracking_thresh = GetJsonFloat(json_str, "tracking_thresh", 0.01f);
    config->sustain_thresh = GetJsonFloat(json_str, "sustain_thresh", 0.1f);
    config->transient_delta = GetJsonFloat(json_str, "transient_delta", 0.05f);

    config->duck_burst_level = GetJsonFloat(json_str, "duck_burst_level", 0.2f);
    config->duck_attack_coef = GetJsonFloat(json_str, "duck_attack_coef", 0.99f);
    config->duck_release_coef = GetJsonFloat(json_str, "duck_release_coef", 0.999f);

    config->burst_duration_ticks = GetJsonInt(json_str, "burst_duration_ticks", 10);
    config->burst_immediate_count = GetJsonInt(json_str, "burst_immediate_count", 3);

    config->density_burst = GetJsonFloat(json_str, "density_burst", 50.0f);
    config->density_sustain = GetJsonFloat(json_str, "density_sustain", 15.0f);
    config->density_decay = GetJsonFloat(json_str, "density_decay", 5.0f);

    config->sustain_read_center_offset_samples = GetJsonInt(json_str, "sustain_read_center_offset_samples", 22050);

    // Micro Attack
    config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_min = GetJsonFloat(json_str, "micro_duration_ms_min", 5.0f);
    config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_max = GetJsonFloat(json_str, "micro_duration_ms_max", 15.0f);
    config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].offset_samples = GetJsonInt(json_str, "micro_offset_samples", 441);
    config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].jitter_samples = GetJsonInt(json_str, "micro_jitter_samples", 100);
    config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].window_type = WINDOW_TYPE_HANN;

    // Short Intermediate
    config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_min = GetJsonFloat(json_str, "short_duration_ms_min", 20.0f);
    config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_max = GetJsonFloat(json_str, "short_duration_ms_max", 50.0f);
    config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].offset_samples = GetJsonInt(json_str, "short_offset_samples", 4410);
    config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].jitter_samples = GetJsonInt(json_str, "short_jitter_samples", 500);
    config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].window_type = WINDOW_TYPE_HANN;

    // Sustain Body
    config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_min = GetJsonFloat(json_str, "body_duration_ms_min", 80.0f);
    config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_max = GetJsonFloat(json_str, "body_duration_ms_max", 200.0f);
    config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].offset_samples = GetJsonInt(json_str, "body_offset_samples", 22050);
    config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].jitter_samples = GetJsonInt(json_str, "body_jitter_samples", 4410);
    config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].window_type = WINDOW_TYPE_TUKEY_LIKE;

    // Output Mix Macros
    *master_dry = GetJsonFloat(json_str, "master_dry_gain", 1.0f);
    *master_wet = GetJsonFloat(json_str, "master_wet_gain", 1.0f);

    free(json_str);
}

// --- Main Execution ---

int main(int argc, char** argv) {
    if (argc != 4) {
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
    }

    const char* input_wav_file = argv[1];
    const char* preset_json_file = argv[2];
    const char* output_wav_file = argv[3];

    // Load DSP Configuration
    EngineConfig_t config = {0};
    float master_dry = 1.0f;
    float master_wet = 1.0f;
    LoadPreset(preset_json_file, &config, &master_dry, &master_wet);

    // Read Input Audio
    WavData in_wav = ReadWav(input_wav_file);
    uint32_t num_frames = in_wav.numSamples;

    // Convert stereo to mono for input if necessary
    float* mono_in = (float*)malloc(num_frames * sizeof(float));
    if (!mono_in) {
        free(in_wav.data);
        ErrorExit("Failed to allocate memory for mono conversion.");
    }

    if (in_wav.numChannels == 2) {
        for (uint32_t i = 0; i < num_frames; i++) {
            mono_in[i] = 0.5f * (in_wav.data[i * 2] + in_wav.data[i * 2 + 1]);
        }
    } else {
        memcpy(mono_in, in_wav.data, num_frames * sizeof(float));
    }
    free(in_wav.data);

    // Prepare Output Buffers
    float* out_left = (float*)malloc(num_frames * sizeof(float));
    float* out_right = (float*)malloc(num_frames * sizeof(float));
    if (!out_left || !out_right) {
        free(mono_in);
        if (out_left) free(out_left);
        if (out_right) free(out_right);
        ErrorExit("Failed to allocate memory for output buffers.");
    }

    // Initialize DSP Core
    int16_t* delay_buffer = (int16_t*)calloc(BUBBLES_BUFFER_SIZE_SAMPLES, sizeof(int16_t));
    if (!delay_buffer) {
        free(mono_in);
        free(out_left);
        free(out_right);
        ErrorExit("Failed to allocate DSP delay buffer.");
    }

    SoundBubblesEngine_t engine;
    SoundBubbles_Init(&engine, delay_buffer, &config);
    engine.master_dry_gain = master_dry;
    engine.master_wet_gain = master_wet;

    // Process Block by Block
    uint32_t processed = 0;
    while (processed < num_frames) {
        uint32_t chunk = BUBBLES_BLOCK_SIZE;
        if (processed + chunk > num_frames) {
            chunk = num_frames - processed;
        }

        SoundBubbles_ProcessBlock(&engine, &mono_in[processed], &out_left[processed], &out_right[processed], chunk);
        processed += chunk;
    }

    // Write Output Audio
    WriteWav(output_wav_file, out_left, out_right, num_frames);

    // Cleanup
    free(mono_in);
    free(out_left);
    free(out_right);
    free(delay_buffer);

    printf("Successfully rendered %u frames to %s\n", num_frames, output_wav_file);
    return EXIT_SUCCESS;
}
