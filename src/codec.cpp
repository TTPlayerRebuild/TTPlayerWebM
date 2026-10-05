#include "codec.h"
#include <cmath>
namespace ttp::webm {
namespace {
unsigned le16(const BYTE *p) { return p[0] | unsigned(p[1]) << 8; }
ogg_packet packet(const Bytes &b) {
    ogg_packet p{};
    p.packet = const_cast<BYTE *>(b.data());
    p.bytes = long(b.size());
    return p;
}
} // namespace
void Codec::clear_decoder() {
    if (opus_) {
        opus_multistream_decoder_destroy(opus_);
        opus_ = nullptr;
    }
    if (block_ready_) {
        vorbis_block_clear(&block_);
        block_ready_ = false;
    }
    if (synthesis_) {
        vorbis_dsp_clear(&dsp_);
        synthesis_ = false;
    }
}
Codec::~Codec() {
    clear_decoder();
    if (vorbis_) {
        vorbis_comment_clear(&comments_);
        vorbis_info_clear(&info_);
    }
}
void Codec::open(bool is_opus, const BYTE *data, size_t bytes) {
    require(!vorbis_ && private_.empty(), E_UNEXPECTED);
    require(data && bytes && bytes <= 16 * 1024 * 1024);
    private_.assign(data, data + bytes);
    opus = is_opus;
    if (opus) {
        require(bytes >= 19 && !memcmp(data, "OpusHead", 8) && data[8] <= 15);
        channels = data[9];
        rate = 48000;
        pre_skip = le16(data + 10);
        require(channels >= 1 && channels <= 8);
        gain_ = static_cast<short>(le16(data + 16));
        family_ = data[18];
        require(family_ <= 1, E_NOTIMPL);
        if (!family_) {
            require(channels <= 2);
            streams_ = 1;
            coupled_ = channels - 1;
            map_[0] = 0;
            map_[1] = 1;
        } else {
            require(bytes >= 21 + channels);
            streams_ = data[19];
            coupled_ = data[20];
            require(streams_ && coupled_ <= streams_ && streams_ + coupled_ <= 255);
            for (unsigned c = 0; c < channels; ++c) {
                map_[c] = data[21 + c];
                require(map_[c] == 255 || map_[c] < streams_ + coupled_);
            }
        }
    } else {
        require(data[0] == 2);
        size_t at = 1, lengths[3]{};
        for (unsigned i = 0; i < 2; ++i) {
            unsigned n;
            do {
                require(at < bytes);
                n = data[at++];
                lengths[i] += n;
            } while (n == 255);
        }
        require(lengths[0] <= bytes - at && lengths[1] <= bytes - at - lengths[0]);
        lengths[2] = bytes - at - lengths[0] - lengths[1];
        vorbis_info_init(&info_);
        vorbis_comment_init(&comments_);
        vorbis_ = true;
        for (int i = 0; i < 3; ++i) {
            require(lengths[i]);
            ogg_packet p{};
            p.packet = const_cast<BYTE *>(data + at);
            p.bytes = long(lengths[i]);
            p.b_o_s = i == 0;
            p.packetno = i;
            require(vorbis_synthesis_headerin(&info_, &comments_, &p) == 0);
            at += lengths[i];
        }
        require(info_.channels >= 1 && info_.channels <= 8 && info_.rate >= 8000 &&
                info_.rate <= 384000);
        channels = unsigned(info_.channels);
        rate = unsigned(info_.rate);
    }
    reset();
}
void Codec::reset() {
    clear_decoder();
    if (opus) {
        int error{};
        opus_ = opus_multistream_decoder_create(48000, int(channels), int(streams_), int(coupled_),
                                                map_, &error);
        require(opus_ && error == OPUS_OK);
        require(opus_multistream_decoder_ctl(opus_, OPUS_SET_GAIN(gain_)) == OPUS_OK);
    } else {
        require(vorbis_synthesis_init(&dsp_, &info_) == 0);
        synthesis_ = true;
        require(vorbis_block_init(&dsp_, &block_) == 0);
        block_ready_ = true;
    }
}
int Codec::packet_samples(const Bytes &b, int previous, int &current) const {
    require(!b.empty() && b.size() <= 16 * 1024 * 1024);
    if (opus) {
        const int n = opus_packet_get_nb_samples(b.data(), opus_int32(b.size()), 48000);
        require(n > 0 && n <= 5760);
        current = 0;
        return n;
    }
    auto p = packet(b);
    const long n = vorbis_packet_blocksize(const_cast<vorbis_info *>(&info_), &p);
    require(n > 0 && n <= 8192);
    current = int(n);
    return previous ? (previous + current) / 4 : 0;
}
void Codec::decode(const Bytes &b, std::vector<float> &pcm) {
    if (opus) {
        pcm.resize(5760 * channels);
        const int n = opus_multistream_decode_float(opus_, b.data(), opus_int32(b.size()),
                                                    pcm.data(), 5760, 0);
        require(n > 0);
        pcm.resize(size_t(n) * channels);
    } else {
        auto p = packet(b);
        p.granulepos = -1;
        require(vorbis_synthesis(&block_, &p) == 0 &&
                vorbis_synthesis_blockin(&dsp_, &block_) == 0);
        float **source{};
        const int n = vorbis_synthesis_pcmout(&dsp_, &source);
        require(n >= 0 && n <= 8192);
        pcm.resize(size_t(n) * channels);
        for (int i = 0; i < n; ++i)
            for (unsigned c = 0; c < channels; ++c)
                pcm[size_t(i) * channels + c] = source[c][i];
        require(vorbis_synthesis_read(&dsp_, n) == 0);
    }
    // Both codecs' family-1 channel order is Vorbis order. The host consumes
    // standard Windows speaker order, including its float64 output path.
    constexpr unsigned order[8][8]{{0},
                                   {0, 1},
                                   {0, 2, 1},
                                   {0, 1, 2, 3},
                                   {0, 2, 1, 3, 4},
                                   {0, 2, 1, 5, 3, 4},
                                   {0, 2, 1, 6, 5, 3, 4},
                                   {0, 2, 1, 7, 5, 6, 3, 4}};
    for (size_t i = 0; i < pcm.size(); i += channels) {
        float frame[8];
        std::copy_n(pcm.data() + i, channels, frame);
        for (unsigned c = 0; c < channels; ++c)
            pcm[i + c] = frame[order[channels - 1][c]];
    }
}
} // namespace ttp::webm
