#include "BubblesFontData.h"

namespace BubblesFontData
{
const juce::MemoryBlock& data(Weight weight)
{
    static const auto decode = [](const char* encoded) {
        juce::MemoryOutputStream compressed;
        const auto decoded = juce::Base64::convertFromBase64(compressed, encoded);
        jassert(decoded);
        juce::MemoryInputStream input(compressed.getData(), compressed.getDataSize(), false);
        juce::GZIPDecompressorInputStream gzip(&input, false,
            juce::GZIPDecompressorInputStream::gzipFormat);
        juce::MemoryBlock font;
        gzip.readIntoMemoryBlock(font);
        jassert(!font.isEmpty());
        return font;
    };

    static const auto light = decode(
#include "MontserratLightData.inc"
    );
    static const auto regular = decode(
#include "MontserratRegularData.inc"
    );
    static const auto bold = decode(
#include "MontserratBoldData.inc"
    );

    return weight == Weight::light ? light : (weight == Weight::bold ? bold : regular);
}
}
