#pragma once

#include <juce_core/juce_core.h>

namespace BubblesFontData
{
    enum class Weight { light, regular, bold };

    // The embedded payloads are gzip-compressed and represented as source text,
    // so repositories and patch tooling never need to transport binary font blobs.
    const juce::MemoryBlock& data(Weight weight);
}
