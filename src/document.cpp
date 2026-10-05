#include "document.h"
#include <cmath>
#include <map>
namespace ttp::webm {
namespace {
constexpr GUID read_id{
    0x83e2bbbf, 0x4fec, 0x4e00, {0xa6, 0x2c, 0x62, 0x61, 0xfe, 0x2b, 0xa2, 0xd4}};
constexpr GUID callback_id{
    0x1717a4a7, 0xa3dc, 0x416b, {0xa2, 0x97, 0x72, 0x16, 0x74, 0x9b, 0xd7, 0xbf}};
uint64_t samples(uint64_t ns, unsigned rate) {
    require(ns / 1000000000 <= UINT64_MAX / rate);
    return ns / 1000000000 * rate + (ns % 1000000000 * rate + 500000000) / 1000000000;
}
} // namespace
void Source::open(IStream *stream) {
    require(stream, E_POINTER);
    require(event_, E_OUTOFMEMORY);
    require(!stream_, E_UNEXPECTED);
    stream_ = ComPtr<IStream>(stream, true);
    stream->QueryInterface(read_id, reinterpret_cast<void **>(extended_.put()));
    LARGE_INTEGER zero{};
    ULARGE_INTEGER end{};
    require(SUCCEEDED(stream->Seek(zero, STREAM_SEEK_END, &end)), E_NOTIMPL);
    size = end.QuadPart;
    require(size >= 16 && size <= INT64_MAX);
    check(stream->Seek(zero, STREAM_SEEK_SET, nullptr));
}
int Source::Read(long long pos, long n, unsigned char *data) {
    if (FAILED(error))
        return -1;
    const auto hr = protect([&]() -> HRESULT {
        require(pos >= 0 && n >= 0 && (data || !n) && uint64_t(pos) <= size &&
                uint64_t(n) <= size - uint64_t(pos));
        // libwebm frequently requests one byte. A bounded read cache avoids an
        // IStream seek/read for each EBML variable integer byte.
        while (n) {
            if (cached_at_ == UINT64_MAX || uint64_t(pos) < cached_at_ ||
                uint64_t(pos) - cached_at_ >= cache_.size()) {
                cached_at_ = uint64_t(pos);
                cache_.resize(size_t(std::min<uint64_t>(65536, size - cached_at_)));
                seek(stream_.p, cached_at_);
                size_t done{};
                while (done < cache_.size()) {
                    ULONG got{};
                    const ULONG want = ULONG(cache_.size() - done);
                    HRESULT result;
                    if (extended_) {
                        using Fn = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, void *, ULONG, ULONG *,
                                                                HANDLE, DWORD);
                        auto **v = *reinterpret_cast<void ***>(extended_.p);
                        result = reinterpret_cast<Fn>(v[14])(extended_.p, cache_.data() + done,
                                                             want, &got, event_, 60000);
                    } else
                        result = stream_->Read(cache_.data() + done, want, &got);
                    check(result);
                    require(got && got <= want, STG_E_READFAULT);
                    done += got;
                }
            }
            const auto offset = size_t(uint64_t(pos) - cached_at_);
            const auto count = std::min<size_t>(size_t(n), cache_.size() - offset);
            memcpy(data, cache_.data() + offset, count);
            pos += count;
            data += count;
            n -= long(count);
        }
        return S_OK;
    });
    if (FAILED(hr)) {
        // A failed refill may have written only part of the cache. Seeking
        // after a transient I/O failure must never reuse that partial data.
        error = hr;
        cached_at_ = UINT64_MAX;
        cache_.clear();
    }
    return FAILED(hr) ? -1 : 0;
}
int Source::Length(long long *total, long long *available) {
    *total = *available = static_cast<long long>(size);
    return 0;
}
Bytes Source::bytes(uint64_t at, size_t n) {
    require(n <= 16 * 1024 * 1024);
    Bytes b(n);
    verify(Read(static_cast<long long>(at), long(n), b.data()));
    return b;
}
void Source::verify(long long code) const {
    check(error);
    require(code == 0);
}
HRESULT Source::callback(IUnknown *value) {
    if (!stream_)
        return E_UNEXPECTED;
    ComPtr<IUnknown> target;
    if (FAILED(stream_->QueryInterface(callback_id, reinterpret_cast<void **>(target.put()))))
        return S_OK;
    using Fn = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
    auto **v = *reinterpret_cast<void ***>(target.p);
    return reinterpret_cast<Fn>(v[4])(target.p, value);
}
Element Document::element(uint64_t at, uint64_t end) {
    require(++elements_ <= 4000000 && at < end && end <= source.size);
    const auto vint = [&](bool id, uint64_t &p, bool &unknown) {
        require(p < end);
        BYTE first{};
        source.verify(source.Read(static_cast<long long>(p++), 1, &first));
        require(first);
        unsigned n = 1, mask = 128;
        while (!(first & mask)) {
            ++n;
            mask >>= 1;
        }
        require(n <= (id ? 4u : 8u) && n - 1 <= end - p);
        uint64_t value = id ? first : first & (mask - 1);
        const auto tail = source.bytes(p, n - 1);
        p += n - 1;
        for (auto b : tail)
            value = (value << 8) | b;
        unknown = !id && value == ((uint64_t(1) << (7 * n)) - 1);
        return value;
    };
    Element e;
    e.begin = at;
    bool ignored{};
    e.id = vint(true, at, ignored);
    auto length = vint(false, at, e.unknown);
    e.data = at;
    require(e.unknown || length <= end - at);
    e.end = e.unknown ? end : at + length;
    return e;
}
void Document::each(uint64_t begin, uint64_t end,
                    const std::function<void(const Element &)> &visit) {
    while (begin < end) {
        auto e = element(begin, end);
        require(!e.unknown);
        visit(e);
        begin = e.end;
    }
}
uint64_t Document::number(const Element &e) {
    require(e.end > e.data && e.end - e.data <= 8);
    uint64_t value{};
    for (auto b : source.bytes(e.data, size_t(e.end - e.data)))
        value = (value << 8) | b;
    return value;
}
std::wstring Document::string(const Element &e) {
    require(e.end - e.data <= 1024 * 1024);
    auto b = source.bytes(e.data, size_t(e.end - e.data));
    while (!b.empty() && !b.back())
        b.pop_back();
    return wide(std::string(b.begin(), b.end()));
}
void Document::simple_tag(const Element &e, unsigned depth, bool targeted) {
    require(depth <= 8 && tags.size() < 4096);
    std::wstring key, value;
    bool has_value{};
    each(e.data, e.end, [&](const Element &child) {
        if (child.id == 0x45a3)
            key = string(child);
        else if (child.id == 0x4487) {
            value = string(child);
            has_value = true;
        } else if (child.id == 0x67c8)
            simple_tag(child, depth + 1, targeted);
    });
    if (key.empty() || !has_value)
        return;
    for (auto &ch : key)
        if (ch >= L'a' && ch <= L'z')
            ch -= 32;
    if (key == L"DATE_RELEASED" || key == L"DATE")
        key = L"Year";
    else if (key == L"PART_NUMBER" || key == L"TRACKNUMBER")
        key = L"Track";
    else if (key == L"ALBUM_ARTIST")
        key = L"AlbumArtist";
    tags.push_back({std::move(key), std::move(value), targeted});
}
void Document::attachment(const Element &e) {
    require(covers.size() < 64);
    Cover c;
    c.origin = e.begin;
    std::wstring name;
    bool description_present{};
    Element data;
    each(e.data, e.end, [&](const Element &x) {
        if (x.id == 0x466e)
            name = string(x);
        else if (x.id == 0x4660)
            c.mime = string(x);
        else if (x.id == 0x467e) {
            c.description = string(x);
            description_present = true;
        } else if (x.id == 0x465c)
            data = x;
        else if (x.id == 0x46ae)
            c.uid = number(x);
    });
    if ((c.mime != L"image/png" && c.mime != L"image/jpeg") || !data.id)
        return;
    require(data.end - data.data <= 16 * 1024 * 1024);
    c.data = source.bytes(data.data, size_t(data.end - data.data));
    if (c.mime == L"image/png") {
        if (c.data.size() < 24 || memcmp(c.data.data(), "\x89PNG\r\n\x1a\n", 8))
            return;
    } else if (c.data.size() < 3 || c.data[0] != 255 || c.data[1] != 216 || c.data[2] != 255)
        return;
    if (!description_present)
        c.description = name;
    c.name = name;
    for (auto &ch : name)
        if (ch >= L'A' && ch <= L'Z')
            ch += 32;
    if (name.find(L"back") != std::wstring::npos)
        c.type = 4;
    if (name.find(L"ttplayer-cover-") == 0) {
        wchar_t *end{};
        auto type = wcstoul(name.c_str() + 15, &end, 10);
        if (end && *end == L'-' && type <= 20)
            c.type = DWORD(type);
    }
    original_cover_origins.push_back(c.origin);
    original_cover_uids.push_back(c.uid);
    covers.push_back(std::move(c));
}
void Document::metadata(uint64_t begin, uint64_t end, uint64_t uid) {
    while (begin < end) {
        auto e = element(begin, end);
        if (e.unknown) {
            require(e.id == 0x1f43b675);
            const auto it = std::lower_bound(clusters_.begin(), clusters_.end(),
                                             std::make_pair(begin, uint64_t(0)));
            require(it != clusters_.end() && it->first == begin && it->second > begin);
            e.end = it->second;
        }
        top.push_back(e);
        if (e.id == 0x1254c367)
            each(e.data, e.end, [&](const Element &tag) {
                if (tag.id != 0x7373)
                    return;
                std::vector<uint64_t> targets;
                bool other_target = false;
                each(tag.data, tag.end, [&](const Element &x) {
                    if (x.id == 0x63c0)
                        each(x.data, x.end, [&](const Element &y) {
                            if (y.id == 0x63c5)
                                targets.push_back(number(y));
                            if (y.id == 0x63c4 || y.id == 0x63c6 || y.id == 0x63c9 ||
                                y.id == 0x63c7)
                                other_target = true;
                        });
                });
                if (other_target ||
                    (!targets.empty() &&
                     std::find(targets.begin(), targets.end(), uid) == targets.end() &&
                     std::find(targets.begin(), targets.end(), 0) == targets.end()))
                    return;
                each(tag.data, tag.end, [&](const Element &x) {
                    if (x.id == 0x67c8)
                        simple_tag(x, 0,
                                   std::find(targets.begin(), targets.end(), 0) == targets.end() &&
                                       std::find(targets.begin(), targets.end(), uid) !=
                                           targets.end());
                });
            });
        else if (e.id == 0x1941a469)
            each(e.data, e.end, [&](const Element &x) {
                if (x.id == 0x61a7)
                    attachment(x);
            });
        begin = e.end;
    }
    std::stable_sort(covers.begin(), covers.end(),
                     [](const Cover &a, const Cover &b) { return a.type == 3 && b.type != 3; });
}
void Document::open(IStream *stream) {
    source.open(stream);
    mkvparser::EBMLHeader header;
    long long position{};
    source.verify(header.Parse(&source, position));
    require(header.m_docType && !strcmp(header.m_docType, "webm") &&
            header.m_docTypeReadVersion <= 4);
    mkvparser::Segment *raw{};
    source.verify(mkvparser::Segment::CreateInstance(&source, position, raw));
    std::unique_ptr<mkvparser::Segment> segment(raw);
    require(raw);
    const auto end = raw->m_size < 0 ? source.size : uint64_t(raw->m_start) + uint64_t(raw->m_size);
    require(end <= source.size);
    segment_header = uint64_t(raw->m_element_start);
    segment_data = uint64_t(raw->m_start);
    segment_end = end;
    safe_layout = end == source.size;
    std::map<long, std::pair<bool, bool>> flags;
    // Bound headers before the upstream parser allocates codec-private data.
    for (uint64_t at = uint64_t(raw->m_start); at < end;) {
        auto e = element(at, end);
        if (e.id == 0x1f43b675)
            break;
        require(!e.unknown);
        if (e.id == 0x1654ae6b) {
            require(e.end - e.data <= 32 * 1024 * 1024);
            each(e.data, e.end, [&](const Element &t) {
                if (t.id == 0xae) {
                    long number_{};
                    bool enabled = true, def = true;
                    each(t.data, t.end, [&](const Element &x) {
                        if (x.id == 0xd7) {
                            auto v = number(x);
                            require(v <= LONG_MAX);
                            number_ = long(v);
                        } else if (x.id == 0xb9)
                            enabled = number(x) != 0;
                        else if (x.id == 0x88)
                            def = number(x) != 0;
                    });
                    flags[number_] = {enabled, def};
                }
            });
        } else
            require(e.end - e.data <= 32 * 1024 * 1024 || e.id == 0xec);
        at = e.end;
    }
    source.verify(raw->Load());
    require(raw->GetCount() <= 1000000);
    const auto *tracks = raw->GetTracks();
    require(tracks && tracks->GetTracksCount() <= 256);
    for (unsigned long i = 0; i < tracks->GetTracksCount(); ++i)
        if (auto *t = tracks->GetTrackByIndex(i)) {
            const auto uid = uint64_t(t->GetUid());
            if (!uid || std::find(track_uids.begin(), track_uids.end(), uid) != track_uids.end())
                safe_layout = false;
            track_uids.push_back(uid);
        }
    const mkvparser::AudioTrack *selected{};
    for (unsigned long i = 0; i < tracks->GetTracksCount(); ++i) {
        auto *t = tracks->GetTrackByIndex(i);
        if (!t || t->GetType() != mkvparser::Track::kAudio)
            continue;
        const char *id = t->GetCodecId();
        if (!id || (strcmp(id, "A_OPUS") && strcmp(id, "A_VORBIS")))
            continue;
        auto f = flags.find(t->GetNumber());
        if (f != flags.end() && !f->second.first)
            continue;
        if (!selected)
            selected = static_cast<const mkvparser::AudioTrack *>(t);
        if (f == flags.end() || f->second.second) {
            selected = static_cast<const mkvparser::AudioTrack *>(t);
            break;
        }
    }
    require(selected, E_NOTIMPL);
    require(!selected->GetContentEncodingCount(), E_NOTIMPL);
    size_t private_size{};
    auto *private_data = selected->GetCodecPrivate(private_size);
    codec.open(!strcmp(selected->GetCodecId(), "A_OPUS"), private_data, private_size);
    require(selected->GetChannels() == codec.channels);
    uint64_t delay = samples(selected->GetCodecDelay(), codec.rate);
    if (codec.opus) {
        if (!delay)
            delay = codec.pre_skip;
        require(delay <= 65535 &&
                (delay > codec.pre_skip ? delay - codec.pre_skip : codec.pre_skip - delay) <= 1);
    }
    // One second also allows Opus prediction/post-filter state to settle on
    // sustained tones; the container's 80 ms minimum need not be a maximum.
    preroll = std::max<uint64_t>(codec.opus ? 48000 : 8192,
                                 samples(selected->GetSeekPreRoll(), codec.rate));
    require(preroll <= uint64_t(codec.rate) * 10);
    int previous_block{};
    uint64_t raw_frames{}, gap_frames{};
    long long first_time = -1;
    // Vorbis synthesis already drops the first half-block while priming its
    // overlap buffer. CodecDelay describes that priming in the packet timeline;
    // subtracting it again from decoded PCM removes real samples.
    if (!codec.opus) {
        raw_frames = delay;
        delay = 0;
    }
    const uint64_t tolerance = std::max<uint64_t>(
        2, samples(uint64_t(raw->GetInfo()->GetTimeCodeScale()) * 2, codec.rate));
    for (const auto *cluster = raw->GetFirst(); cluster && !cluster->EOS();
         cluster = raw->GetNext(cluster)) {
        const mkvparser::BlockEntry *entry{};
        source.verify(cluster->GetFirst(entry));
        while (entry && !entry->EOS()) {
            const auto *b = entry->GetBlock();
            require(b);
            if (b->GetTrackNumber() == selected->GetNumber()) {
                const auto time = b->GetTime(cluster);
                require(time >= 0);
                const bool first_packet = first_time < 0;
                if (first_packet)
                    first_time = time;
                const auto time_frames = samples(uint64_t(time - first_time), codec.rate);
                if (time_frames > raw_frames + gap_frames + tolerance)
                    gap_frames = time_frames - raw_frames;
                else if (!first_packet)
                    require(time_frames + tolerance >= raw_frames + gap_frames);
                const auto first = packets.size();
                require(b->GetFrameCount() > 0 && b->GetFrameCount() <= 256);
                for (int j = 0; j < b->GetFrameCount(); ++j) {
                    require(packets.size() < 4000000);
                    auto f = b->GetFrame(j);
                    require(f.pos >= 0 && f.len > 0);
                    auto bytes = source.bytes(uint64_t(f.pos), size_t(f.len));
                    int block{};
                    unsigned n = unsigned(codec.packet_samples(bytes, previous_block, block));
                    previous_block = block;
                    Packet p;
                    p.offset = uint64_t(f.pos);
                    p.bytes = DWORD(f.len);
                    p.samples = n;
                    p.drop = unsigned(std::min<uint64_t>(delay, n));
                    delay -= p.drop;
                    p.take = n - p.drop;
                    p.start = frames + gap_frames;
                    frames += p.take;
                    raw_frames += n;
                    audio_bytes += p.bytes;
                    packets.push_back(p);
                }
                const auto discard = b->GetDiscardPadding();
                require(discard != INT64_MIN);
                auto trim = samples(uint64_t(discard < 0 ? -discard : discard), codec.rate);
                if (discard < 0) {
                    for (size_t j = first; j < packets.size() && trim; ++j) {
                        auto &p = packets[j];
                        auto n = unsigned(std::min<uint64_t>(trim, p.take));
                        p.drop += n;
                        p.take -= n;
                        trim -= n;
                        frames -= n;
                        for (size_t k = j + 1; k < packets.size(); ++k)
                            packets[k].start -= n;
                    }
                } else {
                    for (size_t j = packets.size(); j > first && trim;) {
                        auto &p = packets[--j];
                        auto n = unsigned(std::min<uint64_t>(trim, p.take));
                        p.take -= n;
                        trim -= n;
                        frames -= n;
                        for (size_t k = j + 1; k < packets.size(); ++k)
                            packets[k].start -= n;
                    }
                }
                require(trim == 0);
            }
            const mkvparser::BlockEntry *next{};
            source.verify(cluster->GetNext(entry, next));
            entry = next;
        }
        require(cluster->GetElementSize() > 0);
        clusters_.emplace_back(uint64_t(cluster->m_element_start),
                               uint64_t(cluster->m_element_start + cluster->GetElementSize()));
    }
    frames += gap_frames;
    require(!packets.empty() && frames && delay == 0);
    // Indexing retains only audio packet offsets; freeing Segment drops video
    // block objects and all upstream cluster allocations before playback.
    metadata(uint64_t(raw->m_start), end, selected->GetUid());
    track_uid = uint64_t(selected->GetUid());
    // Track-specific fields override inherited global fields. Empty values can
    // intentionally hide a deleted inherited title without changing other tracks.
    std::vector<std::wstring> specific;
    for (const auto &t : tags)
        if (t.targeted)
            specific.push_back(canonical_tag(t.key));
    tags.erase(std::remove_if(tags.begin(), tags.end(),
                              [&](const Tag &t) {
                                  return !t.targeted &&
                                         std::find(specific.begin(), specific.end(),
                                                   canonical_tag(t.key)) != specific.end();
                              }),
               tags.end());
    const char *title = selected->GetNameAsUTF8();
    if (!title || !*title)
        title = raw->GetInfo()->GetTitleAsUTF8();
    if (title) {
        bool exists = false;
        for (auto &t : tags)
            if (!_wcsicmp(t.key.c_str(), L"Title"))
                exists = true;
        if (!exists && *title)
            tags.push_back({L"Title", wide(title)});
    }
    tags.erase(
        std::remove_if(tags.begin(), tags.end(), [](const Tag &t) { return t.value.empty(); }),
        tags.end());
    clusters_.clear();
    clusters_.shrink_to_fit();
    codec.reset();
    capture_file();
}
} // namespace ttp::webm
