#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace Tempest { class SoundStreamSource; }

// Ogg Opus support for repacked game archives: audio keeps its original *.WAV name inside the VDF,
// only the payload is Opus. Decoded into an in-memory PCM16 WAV, so Tempest::Sound loads it as usual.
class OpusSound final {
  public:
    static bool                 isOpus(const uint8_t* data, size_t size);
    static std::vector<uint8_t> decodeToWav(const uint8_t* data, size_t size);
    // the same samples as decodeToWav (frames of them after the trim), decoded while the sound plays;
    // nullptr if the stream can not be parsed
    static std::shared_ptr<const Tempest::SoundStreamSource> makeStream(const uint8_t* data, size_t size, uint64_t frames);
  };
