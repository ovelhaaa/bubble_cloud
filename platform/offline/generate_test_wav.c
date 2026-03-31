#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#define SAMPLE_RATE 44100
#define NUM_CHANNELS 1
#define BITS_PER_SAMPLE 16

#define PI 3.14159265358979323846

// WAV header struct
#pragma pack(push, 1)
typedef struct {
    char chunk_id[4];       // "RIFF"
    uint32_t chunk_size;    // 36 + subchunk2_size
    char format[4];         // "WAVE"
    char subchunk1_id[4];   // "fmt "
    uint32_t subchunk1_size;// 16 for PCM
    uint16_t audio_format;  // 1 for PCM
    uint16_t num_channels;  // 1
    uint32_t sample_rate;   // 44100
    uint32_t byte_rate;     // sample_rate * num_channels * bits_per_sample / 8
    uint16_t block_align;   // num_channels * bits_per_sample / 8
    uint16_t bits_per_sample; // 16
    char subchunk2_id[4];   // "data"
    uint32_t subchunk2_size;// num_samples * num_channels * bits_per_sample / 8
} WavHeader;
#pragma pack(pop)

void write_wav(const char* filename, int16_t* buffer, int num_samples) {
    FILE* file = fopen(filename, "wb");
    if (!file) {
        fprintf(stderr, "Error opening %s for writing\n", filename);
        return;
    }

    WavHeader header;
    memcpy(header.chunk_id, "RIFF", 4);
    memcpy(header.format, "WAVE", 4);
    memcpy(header.subchunk1_id, "fmt ", 4);
    header.subchunk1_size = 16;
    header.audio_format = 1;
    header.num_channels = NUM_CHANNELS;
    header.sample_rate = SAMPLE_RATE;
    header.bits_per_sample = BITS_PER_SAMPLE;
    header.byte_rate = SAMPLE_RATE * NUM_CHANNELS * (BITS_PER_SAMPLE / 8);
    header.block_align = NUM_CHANNELS * (BITS_PER_SAMPLE / 8);
    memcpy(header.subchunk2_id, "data", 4);
    header.subchunk2_size = num_samples * NUM_CHANNELS * (BITS_PER_SAMPLE / 8);
    header.chunk_size = 36 + header.subchunk2_size;

    fwrite(&header, sizeof(WavHeader), 1, file);
    fwrite(buffer, sizeof(int16_t), num_samples, file);

    fclose(file);
}

// Simple oscillator
float generate_sine(float phase) {
    return sinf(phase);
}

float generate_triangle(float phase) {
    float norm_phase = fmodf(phase, 2.0f * PI) / (2.0f * PI);
    if (norm_phase < 0.5f) {
        return 4.0f * norm_phase - 1.0f;
    } else {
        return 3.0f - 4.0f * norm_phase;
    }
}

// Add a note with an ADSR envelope
void add_note(int16_t* buffer, int start_sample, int duration_samples, float freq, float amplitude, float attack_ms, float decay_ms, float sustain_level, float release_ms) {
    float phase = 0.0f;
    float phase_inc = 2.0f * PI * freq / SAMPLE_RATE;

    int attack_samples = (int)(attack_ms * SAMPLE_RATE / 1000.0f);
    int decay_samples = (int)(decay_ms * SAMPLE_RATE / 1000.0f);
    int release_samples = (int)(release_ms * SAMPLE_RATE / 1000.0f);
    int sustain_samples = duration_samples - attack_samples - decay_samples - release_samples;

    if (sustain_samples < 0) sustain_samples = 0;

    for (int i = 0; i < duration_samples; i++) {
        float env = 0.0f;
        if (i < attack_samples) {
            env = (float)i / attack_samples;
        } else if (i < attack_samples + decay_samples) {
            env = 1.0f - (1.0f - sustain_level) * ((float)(i - attack_samples) / decay_samples);
        } else if (i < attack_samples + decay_samples + sustain_samples) {
            env = sustain_level;
        } else if (i < duration_samples) {
            int release_i = i - (attack_samples + decay_samples + sustain_samples);
            env = sustain_level * (1.0f - (float)release_i / release_samples);
        }

        // Ensure no out of bounds
        if (start_sample + i >= 30 * SAMPLE_RATE) break;

        // Mix triangle and sine for a generic guitar-like tonal sound
        float sample = 0.5f * generate_triangle(phase) + 0.5f * generate_sine(phase);
        sample *= env * amplitude;

        int32_t mixed = buffer[start_sample + i] + (int32_t)(sample * 32767.0f);
        if (mixed > 32767) mixed = 32767;
        if (mixed < -32768) mixed = -32768;
        buffer[start_sample + i] = (int16_t)mixed;

        phase += phase_inc;
    }
}

// Add percussive burst (noise)
void add_burst(int16_t* buffer, int start_sample, float amplitude, float duration_ms) {
    int duration_samples = (int)(duration_ms * SAMPLE_RATE / 1000.0f);
    for (int i = 0; i < duration_samples; i++) {
        if (start_sample + i >= 30 * SAMPLE_RATE) break;
        float noise = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;

        // sharp decay envelope
        float env = expf(-5.0f * (float)i / duration_samples);
        float sample = noise * env * amplitude;

        int32_t mixed = buffer[start_sample + i] + (int32_t)(sample * 32767.0f);
        if (mixed > 32767) mixed = 32767;
        if (mixed < -32768) mixed = -32768;
        buffer[start_sample + i] = (int16_t)mixed;
    }
}

int main() {
    // Seed RNG
    srand(42);

    int total_seconds = 30; // Max buffer size
    int total_samples = total_seconds * SAMPLE_RATE;
    int16_t* buffer = (int16_t*)calloc(total_samples, sizeof(int16_t));

    int current_sample = 0;

    // 1. Initial silence (2s)
    current_sample += 2 * SAMPLE_RATE;

    // 2. Isolated short tonal notes with increasing dynamics (4s)
    add_note(buffer, current_sample, 0.5f * SAMPLE_RATE, 220.0f, 0.2f, 10.0f, 100.0f, 0.5f, 100.0f); // A3, weak
    current_sample += 1 * SAMPLE_RATE;
    add_note(buffer, current_sample, 0.5f * SAMPLE_RATE, 220.0f, 0.5f, 5.0f, 100.0f, 0.5f, 100.0f); // A3, medium
    current_sample += 1 * SAMPLE_RATE;
    add_note(buffer, current_sample, 0.5f * SAMPLE_RATE, 220.0f, 0.9f, 2.0f, 100.0f, 0.5f, 100.0f); // A3, strong
    current_sample += 2 * SAMPLE_RATE;

    // 3. One long sustained/decaying note (5s)
    add_note(buffer, current_sample, 4.0f * SAMPLE_RATE, 196.0f, 0.7f, 15.0f, 1000.0f, 0.3f, 2000.0f); // G3
    current_sample += 5 * SAMPLE_RATE;

    // 4. Repeated short attacks (4s)
    for (int i = 0; i < 8; i++) {
        add_note(buffer, current_sample + (int)(i * 0.5f * SAMPLE_RATE), 0.3f * SAMPLE_RATE, 329.63f, 0.6f, 5.0f, 50.0f, 0.1f, 50.0f); // E4
    }
    current_sample += 4 * SAMPLE_RATE;

    // 5. Percussive/noise bursts (3s)
    add_burst(buffer, current_sample, 0.8f, 50.0f);
    current_sample += 1 * SAMPLE_RATE;
    add_burst(buffer, current_sample, 0.9f, 50.0f);
    current_sample += 1 * SAMPLE_RATE;
    add_burst(buffer, current_sample, 1.0f, 50.0f);
    current_sample += 1 * SAMPLE_RATE;

    // 6. Mixed note + percussive burst events (4s)
    for(int i=0; i<4; i++) {
        add_note(buffer, current_sample + (int)(i * 1.0f * SAMPLE_RATE), 0.8f * SAMPLE_RATE, 146.83f, 0.6f, 10.0f, 200.0f, 0.4f, 200.0f); // D3
        // Add a burst exactly at the start of the note to simulate strong transient
        add_burst(buffer, current_sample + (int)(i * 1.0f * SAMPLE_RATE), 0.7f, 30.0f);
    }
    current_sample += 4 * SAMPLE_RATE;

    // 7. Short musical phrase (4s)
    // E3 - G3 - A3 - C4 - D4
    float freqs[] = {164.81f, 196.0f, 220.0f, 261.63f, 293.66f};
    for(int i=0; i<5; i++) {
        add_note(buffer, current_sample + (int)(i * 0.6f * SAMPLE_RATE), 0.5f * SAMPLE_RATE, freqs[i], 0.7f, 5.0f, 100.0f, 0.5f, 100.0f);
    }
    current_sample += 4 * SAMPLE_RATE;

    // 8. Final silence (2s)
    current_sample += 2 * SAMPLE_RATE; // Leaves buffer empty at end

    write_wav("sample/test_in.wav", buffer, current_sample); // Write exact duration ~ 28 seconds

    free(buffer);
    printf("Successfully generated sample/test_in.wav (%d samples, %.2f seconds)\n", current_sample, (float)current_sample / SAMPLE_RATE);
    return 0;
}
