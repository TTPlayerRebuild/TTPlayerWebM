#pragma once
#include "codec.h"
#include <mkvparser/mkvparser.h>
#include <functional>
#include <map>
namespace ttp::webm {
class Source final : public mkvparser::IMkvReader {
    ComPtr<IStream> stream_;
    ComPtr<IUnknown> extended_;
    HANDLE event_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Bytes cache_;
    uint64_t cached_at_{UINT64_MAX};

  public:
    uint64_t size{};
    HRESULT error{S_OK};
    ~Source() override {
        if (event_)
            CloseHandle(event_);
    }
    void open(IStream *stream);
    int Read(long long pos, long n, unsigned char *data) override;
    int Length(long long *total, long long *available) override;
    Bytes bytes(uint64_t at, size_t n);
    void verify(long long code) const;
    HRESULT callback(IUnknown *value);
    IStream *stream() const { return stream_.p; }
    void close() {
        extended_.reset();
        stream_.reset();
        cache_.clear();
        cached_at_ = UINT64_MAX;
    }
};
struct Packet {
    uint64_t offset{}, start{};
    DWORD bytes{};
    unsigned samples{}, drop{}, take{};
};
struct Tag {
    std::wstring key, value;
    bool targeted{};
};
struct Cover {
    std::wstring mime, description, name;
    Bytes data;
    DWORD type{3};
    uint64_t origin{}, uid{};
    bool changed{};
    Picture view{};
};
struct Element {
    uint64_t id{}, data{}, end{}, begin{};
    bool unknown{};
};
class Document {
    uint64_t elements_{};
    std::vector<std::pair<uint64_t, uint64_t>> clusters_;
    Element element(uint64_t at, uint64_t end);
    void each(uint64_t begin, uint64_t end, const std::function<void(const Element &)> &visit);
    uint64_t number(const Element &e);
    std::wstring string(const Element &e);
    void metadata(uint64_t begin, uint64_t end, uint64_t uid);
    void simple_tag(const Element &e, unsigned depth, bool targeted);
    void attachment(const Element &e);

  public:
    Source source;
    Codec codec;
    std::vector<Packet> packets;
    std::vector<Tag> tags;
    std::vector<Cover> covers;
    std::vector<uint64_t> original_cover_origins;
    std::vector<uint64_t> original_cover_uids;
    uint64_t frames{}, audio_bytes{}, preroll{};
    uint64_t segment_header{}, segment_data{}, segment_end{}, track_uid{};
    std::vector<uint64_t> track_uids;
    std::vector<Element> top;
    std::wstring path;
    DWORD stream_mode{};
    BY_HANDLE_FILE_INFORMATION stamp{};
    bool stamped{}, safe_layout{}, clear_tags{}, covers_changed{};
    std::map<std::wstring, std::wstring> tag_changes;
    void open(IStream *stream);
    void capture_file();
    bool can_edit() const;
    bool dirty() const { return clear_tags || covers_changed || !tag_changes.empty(); }
    void set_tag(const char *key, const wchar_t *value);
    HRESULT save() noexcept;
};
std::wstring canonical_tag(std::wstring key);
Cover edited_cover(const Picture &picture);
} // namespace ttp::webm
