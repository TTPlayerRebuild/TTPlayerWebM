#include "document.h"
#include <cmath>
namespace ttp::webm {
class WebmReader final : public Reader, public Metadata, public Thumbnail, public MetadataCommit {
    LONG references_{1};
    std::unique_ptr<Document> doc_;
    WAVEFORMATEX format_{};
    bool playable_{}, warmup_{};
    size_t next_{}, queue_at_{};
    uint64_t position_{}, queue_start_{};
    std::vector<float> queue_, decoded_;
    HRESULT error_{S_OK};
    bool commit_attempted_{};
    HRESULT commit_result_{S_OK};
    void mutable_metadata() {
        require(doc_ && !commit_attempted_, E_UNEXPECTED);
        require(doc_->can_edit(), STG_E_ACCESSDENIED);
    }
    ~WebmReader() {
        if (doc_ && doc_->dirty() && !commit_attempted_ && FAILED(Commit()))
            OutputDebugStringW(L"ttp_webm: metadata save failed during final Release; original "
                               L"host has no commit result channel.\n");
    }
    void reset(uint64_t target) {
        doc_->codec.reset();
        doc_->source.error = S_OK;
        error_ = S_OK;
        queue_.clear();
        queue_at_ = 0;
        position_ = target;
        if (target >= doc_->frames) {
            next_ = doc_->packets.size();
            return;
        }
        auto it = std::upper_bound(doc_->packets.begin(), doc_->packets.end(), target,
                                   [](uint64_t value, const Packet &p) { return value < p.start; });
        size_t index = it == doc_->packets.begin() ? 0 : size_t(it - doc_->packets.begin() - 1);
        uint64_t back{};
        while (index && back < doc_->preroll)
            back += doc_->packets[--index].samples;
        next_ = index;
        warmup_ = !doc_->codec.opus && index != 0;
    }
    bool fill() {
        queue_.clear();
        queue_at_ = 0;
        while (next_ < doc_->packets.size()) {
            const auto &p = doc_->packets[next_++];
            doc_->codec.decode(doc_->source.bytes(p.offset, p.bytes), decoded_);
            if (warmup_) {
                warmup_ = false;
                continue;
            }
            const unsigned channels = format_.nChannels;
            require(decoded_.size() == size_t(p.samples) * channels);
            if (!p.take || p.start + p.take <= position_)
                continue;
            queue_start_ = p.start;
            queue_.assign(decoded_.begin() + size_t(p.drop) * channels,
                          decoded_.begin() + size_t(p.drop + p.take) * channels);
            if (position_ > queue_start_)
                queue_at_ = size_t(position_ - queue_start_) * channels;
            return true;
        }
        return false;
    }

  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (same(id, IID_IUnknown) || same(id, iid_reader))
            *out = static_cast<Reader *>(this);
        else if (same(id, iid_metadata))
            *out = static_cast<Metadata *>(this);
        else if (same(id, iid_thumbnail))
            *out = static_cast<Thumbnail *>(this);
        else if (same(id, iid_metadata_commit))
            *out = static_cast<MetadataCommit *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = InterlockedDecrement(&references_);
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Commit() override {
        if (commit_attempted_)
            return commit_result_;
        if (!doc_)
            return E_UNEXPECTED;
        commit_attempted_ = true;
        playable_ = false;
        commit_result_ = doc_->save();
        doc_->source.close();
        return commit_result_;
    }
    HRESULT STDMETHODCALLTYPE Open(IStream *stream, DWORD flags) override {
        if (doc_ || commit_attempted_)
            return E_UNEXPECTED;
        return protect([&]() -> HRESULT {
            auto document = std::make_unique<Document>();
            document->open(stream);
            format_.wFormatTag = flags & 2 ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
            format_.wBitsPerSample = flags & 2 ? 64 : 16;
            format_.nChannels = WORD(document->codec.channels);
            format_.nSamplesPerSec = document->codec.rate;
            format_.nBlockAlign = WORD(format_.nChannels * (format_.wBitsPerSample / 8));
            format_.nAvgBytesPerSec = format_.nBlockAlign * format_.nSamplesPerSec;
            doc_ = std::move(document);
            playable_ = (flags & 1) != 0;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Capabilities(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = doc_ ? 2 | (!commit_attempted_ && doc_->can_edit() ? 20 : 0) : 0;
        return doc_ ? S_OK : E_UNEXPECTED;
    }
    HRESULT STDMETHODCALLTYPE Duration(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = doc_ ? milliseconds(doc_->frames, format_.nSamplesPerSec) : 0;
        return doc_ ? S_OK : E_UNEXPECTED;
    }
    HRESULT STDMETHODCALLTYPE Format(WAVEFORMATEX **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!doc_)
            return E_UNEXPECTED;
        auto *p = static_cast<WAVEFORMATEX *>(CoTaskMemAlloc(sizeof(format_)));
        if (!p)
            return E_OUTOFMEMORY;
        *p = format_;
        *out = p;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BufferSize(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = format_.nBlockAlign * 1024;
        return doc_ ? S_OK : E_UNEXPECTED;
    }
    HRESULT STDMETHODCALLTYPE CodecName(wchar_t **out) override {
        return text(doc_ && doc_->codec.opus ? L"OPUS|WebM / Opus Audio"
                                             : L"Vorbis|WebM / Vorbis Audio",
                    out);
    }
    HRESULT STDMETHODCALLTYPE Bitrate(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = 0;
        if (!doc_)
            return E_UNEXPECTED;
        *out =
            DWORD(std::min<long double>(MAXDWORD, static_cast<long double>(doc_->audio_bytes) * 8 *
                                                      format_.nSamplesPerSec / doc_->frames));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BitDepth(WORD *out) override {
        if (!out)
            return E_POINTER;
        *out = 16;
        return doc_ ? S_OK : E_UNEXPECTED;
    }
    HRESULT STDMETHODCALLTYPE SetCallback(IUnknown *p) override {
        return doc_ ? doc_->source.callback(p) : E_UNEXPECTED;
    }
    HRESULT STDMETHODCALLTYPE Start() override { return doc_ ? S_OK : E_UNEXPECTED; }
    HRESULT STDMETHODCALLTYPE Stop() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Read(Buffer *buffer) override {
        if (!buffer)
            return E_POINTER;
        const auto cleared = buffer->SetLength(0);
        if (FAILED(cleared))
            return cleared;
        if (!doc_ || !playable_)
            return E_UNEXPECTED;
        if (FAILED(error_))
            return error_;
        DWORD capacity{}, length{};
        BYTE *data{};
        DWORD used{};
        const auto result = protect([&]() -> HRESULT {
            check(buffer->Capacity(&capacity));
            check(buffer->Data(&data, &length));
            require(capacity >= format_.nBlockAlign, E_INVALIDARG);
            require(data, E_POINTER);
            capacity -= capacity % format_.nBlockAlign;
            while (used < capacity && position_ < doc_->frames) {
                if (queue_at_ >= queue_.size() && !fill()) {
                    require(position_ == doc_->frames);
                    break;
                }
                size_t n = (capacity - used) / format_.nBlockAlign;
                if (position_ < queue_start_) {
                    n = size_t(std::min<uint64_t>(n, queue_start_ - position_));
                    memset(data + used, 0, n * format_.nBlockAlign);
                } else {
                    n = std::min(n, (queue_.size() - queue_at_) / format_.nChannels);
                    for (size_t i = 0; i < n * format_.nChannels; ++i) {
                        const float value = queue_[queue_at_++];
                        if (format_.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
                            const double d = value;
                            memcpy(data + used + i * 8, &d, 8);
                        } else {
                            double scaled =
                                std::isfinite(value) ? std::nearbyint(double(value) * 32768) : 0;
                            const short s = short(std::clamp(scaled, -32768.0, 32767.0));
                            memcpy(data + used + i * 2, &s, 2);
                        }
                    }
                }
                require(n);
                position_ += n;
                used += DWORD(n * format_.nBlockAlign);
            }
            return used ? S_OK : S_FALSE;
        });
        // Deliver preceding valid PCM before reporting a later packet error.
        // Invalid caller arguments do not poison the decoder.
        if (FAILED(result) && result != E_POINTER && result != E_INVALIDARG)
            error_ = result;
        const auto set = buffer->SetLength(used);
        if (FAILED(set))
            return set;
        return used ? S_OK : result;
    }
    HRESULT STDMETHODCALLTYPE Seek(DWORD *milliseconds_) override {
        if (!milliseconds_)
            return E_POINTER;
        if (!doc_ || !playable_)
            return E_UNEXPECTED;
        const auto result = protect([&]() -> HRESULT {
            const DWORD duration = milliseconds(doc_->frames, format_.nSamplesPerSec);
            const auto target =
                *milliseconds_ && *milliseconds_ >= duration
                    ? doc_->frames
                    : std::min<uint64_t>(doc_->frames,
                                         uint64_t(*milliseconds_) * format_.nSamplesPerSec / 1000);
            reset(target);
            *milliseconds_ = milliseconds(target, format_.nSamplesPerSec);
            return S_OK;
        });
        if (FAILED(result))
            error_ = result;
        return result;
    }
    HRESULT STDMETHODCALLTYPE Count(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = doc_ ? DWORD(doc_->tags.size()) : 0;
        return doc_ ? S_OK : E_UNEXPECTED;
    }
    HRESULT STDMETHODCALLTYPE At(DWORD i, wchar_t **key, wchar_t **value) override {
        if (!key || !value)
            return E_POINTER;
        *key = *value = nullptr;
        if (!doc_)
            return E_UNEXPECTED;
        if (i >= doc_->tags.size())
            return E_INVALIDARG;
        auto hr = text(doc_->tags[i].key, key);
        if (FAILED(hr))
            return hr;
        hr = text(doc_->tags[i].value, value);
        if (FAILED(hr)) {
            CoTaskMemFree(*key);
            *key = nullptr;
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Get(const char *key, wchar_t **value) override {
        if (!key || !value)
            return E_POINTER;
        *value = nullptr;
        if (!doc_)
            return E_UNEXPECTED;
        return protect([&]() -> HRESULT {
            const auto k = wide(key);
            std::wstring combined;
            for (const auto &t : doc_->tags)
                if (!_wcsicmp(t.key.c_str(), k.c_str())) {
                    if (!combined.empty())
                        combined += L"; ";
                    combined += t.value;
                }
            return combined.empty() ? S_FALSE : text(combined, value);
        });
    }
    HRESULT STDMETHODCALLTYPE Set(const char *key, const wchar_t *value) override {
        return protect([&]() -> HRESULT {
            mutable_metadata();
            doc_->set_tag(key, value);
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE PictureCount(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = doc_ ? DWORD(doc_->covers.size()) : 0;
        return doc_ ? S_OK : E_UNEXPECTED;
    }
    HRESULT STDMETHODCALLTYPE MaximumBytes(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = 16 * 1024 * 1024;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MaximumCount(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = 64;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PictureAt(DWORD i, const Picture **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!doc_)
            return E_UNEXPECTED;
        if (i >= doc_->covers.size())
            return E_INVALIDARG;
        auto &c = doc_->covers[i];
        c.view = {sizeof(Picture),      c.mime.c_str(), c.description.c_str(),
                  DWORD(c.data.size()), c.data.data(),  c.type};
        *out = &c.view;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ReplacePicture(DWORD index, const Picture *p, DWORD mask) override {
        if (!p)
            return E_POINTER;
        return protect([&]() -> HRESULT {
            mutable_metadata();
            require(p->size >= sizeof(Picture) && index < doc_->covers.size() && !(mask & ~15u),
                    E_INVALIDARG);
            if (!mask)
                return S_OK;
            const auto &old = doc_->covers[index];
            Picture merged{sizeof(Picture),
                           mask & 1 ? p->mime : old.mime.c_str(),
                           mask & 2 ? p->description : old.description.c_str(),
                           mask & 8 ? p->bytes : DWORD(old.data.size()),
                           mask & 8 ? p->data : old.data.data(),
                           mask & 4 ? p->type : old.type};
            auto c = edited_cover(merged);
            c.origin = old.origin;
            c.uid = old.uid;
            if (c.mime == old.mime && c.type == old.type)
                c.name = old.name;
            doc_->covers[index] = std::move(c);
            doc_->covers_changed = true;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE AddPicture(const Picture *p) override {
        if (!p)
            return E_POINTER;
        return protect([&]() -> HRESULT {
            mutable_metadata();
            require(doc_->covers.size() < 64, E_INVALIDARG);
            auto c = edited_cover(*p);
            doc_->covers.push_back(std::move(c));
            doc_->covers_changed = true;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE RemovePicture(DWORD index) override {
        return protect([&]() -> HRESULT {
            mutable_metadata();
            require(index < doc_->covers.size(), E_INVALIDARG);
            doc_->covers.erase(doc_->covers.begin() + index);
            doc_->covers_changed = true;
            return S_OK;
        });
    }
};
HRESULT make_reader(void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return protect([&]() -> HRESULT {
        *out = static_cast<Reader *>(new WebmReader);
        return S_OK;
    });
}
} // namespace ttp::webm
