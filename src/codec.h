#pragma once
#include "common.h"
#include <opus_multistream.h>
#include <vorbis/codec.h>
namespace ttp::webm {
class Codec {
    OpusMSDecoder *opus_{};
    vorbis_info info_{};
    vorbis_comment comments_{};
    vorbis_dsp_state dsp_{};
    vorbis_block block_{};
    bool vorbis_{}, synthesis_{}, block_ready_{};
    Bytes private_;
    unsigned streams_{}, coupled_{}, family_{};
    BYTE map_[8]{};
    short gain_{};
    void clear_decoder();

  public:
    bool opus{};
    unsigned rate{}, channels{}, pre_skip{};
    Codec() = default;
    Codec(const Codec &) = delete;
    Codec &operator=(const Codec &) = delete;
    ~Codec();
    void open(bool is_opus, const BYTE *data, size_t bytes);
    void reset();
    int packet_samples(const Bytes &bytes, int previous_block, int &current_block) const;
    void decode(const Bytes &bytes, std::vector<float> &pcm);
};
} // namespace ttp::webm
