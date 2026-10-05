#include "document.h"
#include <shlwapi.h>
#include <array>
#include <set>

namespace ttp::webm {
namespace {
constexpr size_t limit = 64 * 1024 * 1024;
constexpr uint64_t TAGS = 0x1254c367, ATTACHMENTS = 0x1941a469, SEEKHEAD = 0x114d9b74;
struct Node {
    uint64_t id{};
    Bytes body, raw;
};
void append(Bytes &to, const Bytes &from) {
    require(from.size() <= limit && to.size() <= limit - from.size(), E_OUTOFMEMORY);
    to.insert(to.end(), from.begin(), from.end());
}
Bytes integer(uint64_t value, unsigned width = 0) {
    if (!width) {
        width = 1;
        while (width < 8 && value >> (8 * width))
            ++width;
    }
    Bytes b(width);
    for (unsigned i = 0; i < width; ++i)
        b[width - 1 - i] = BYTE(value >> (i * 8));
    return b;
}
Bytes vint(uint64_t value, unsigned width = 0) {
    if (!width) {
        width = 1;
        while (width < 8 && value >= ((uint64_t(1) << (width * 7)) - 1))
            ++width;
    }
    require(width <= 8 && value < ((uint64_t(1) << (width * 7)) - 1));
    return integer(value | (uint64_t(1) << (width * 7)), width);
}
Bytes ebml(uint64_t id, const Bytes &body) {
    auto out = integer(id);
    append(out, vint(body.size()));
    append(out, body);
    return out;
}
Bytes number(uint64_t id, uint64_t n) { return ebml(id, integer(n)); }
Bytes string(uint64_t id, const std::wstring &s) {
    auto b = utf8(s);
    return ebml(id, Bytes(b.begin(), b.end()));
}
std::vector<Node> children(const Bytes &bytes) {
    std::vector<Node> out;
    size_t at{};
    const auto read = [&](bool id) {
        require(at < bytes.size() && bytes[at]);
        unsigned n = 1, mask = 128, first = bytes[at++];
        while (!(first & mask)) {
            mask >>= 1;
            ++n;
        }
        require(n <= (id ? 4u : 8u) && n - 1 <= bytes.size() - at);
        uint64_t value = id ? first : first & (mask - 1);
        for (unsigned i = 1; i < n; ++i)
            value = value << 8 | bytes[at++];
        require(id || value != ((uint64_t(1) << (n * 7)) - 1));
        return value;
    };
    while (at < bytes.size()) {
        require(out.size() < 16384);
        const auto begin = at;
        auto id = read(true), size = read(false);
        require(size <= bytes.size() - at);
        auto end = at + size_t(size);
        out.push_back({id, Bytes(bytes.begin() + at, bytes.begin() + end),
                       Bytes(bytes.begin() + begin, bytes.begin() + end)});
        at = end;
    }
    return out;
}
std::wstring value(const Node &n) {
    const auto end = std::find(n.body.begin(), n.body.end(), BYTE(0));
    return wide(std::string(n.body.begin(), end));
}
uint64_t value_number(const Node &n) {
    require(!n.body.empty() && n.body.size() <= 8);
    uint64_t v{};
    for (auto c : n.body)
        v = v << 8 | c;
    return v;
}
// Compile-time data avoids dynamic local-static initialization (TLS guards in
// a LoadLibrary-loaded DLL are not available on Windows XP).
constexpr auto crc_table = []() {
    std::array<DWORD, 256> t{};
    for (DWORD n = 0; n < 256; ++n) {
        auto c = n;
        for (int i = 0; i < 8; ++i)
            c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0);
        t[n] = c;
    }
    return t;
}();
DWORD crc_update(DWORD crc, const BYTE *data, size_t size) {
    for (size_t i = 0; i < size; ++i)
        crc = crc_table[(crc ^ data[i]) & 255] ^ (crc >> 8);
    return crc;
}
Bytes crc_bytes(DWORD crc) { return {BYTE(crc), BYTE(crc >> 8), BYTE(crc >> 16), BYTE(crc >> 24)}; }
Bytes rebuild(const Node &original, Bytes body) {
    bool crc{};
    for (const auto &n : children(original.body))
        if (n.id == 0xbf) {
            require(!crc && n.body.size() == 4);
            crc = true;
        }
    if (crc) {
        auto next =
            ebml(0xbf, crc_bytes(crc_update(0xffffffff, body.data(), body.size()) ^ 0xffffffff));
        append(next, body);
        body = std::move(next);
    }
    return ebml(original.id, body);
}
Node load(Document &doc, const Element &e) {
    require(e.end - e.begin <= limit, E_NOTIMPL);
    Bytes raw(size_t(e.end - e.begin));
    doc.source.verify(
        doc.source.Read(static_cast<long long>(e.begin), long(raw.size()), raw.data()));
    auto list = children(raw);
    require(list.size() == 1);
    return std::move(list[0]);
}
struct Target {
    bool applies{}, global{}, other{};
    std::vector<uint64_t> ids;
};
Target target(const Node &tag, uint64_t uid) {
    Target t;
    unsigned count{};
    for (const auto &n : children(tag.body))
        if (n.id == 0x63c0) {
            require(++count == 1);
            for (const auto &c : children(n.body)) {
                if (c.id == 0x63c5)
                    t.ids.push_back(value_number(c));
                if (c.id == 0x63c4 || c.id == 0x63c6 || c.id == 0x63c9 || c.id == 0x63c7)
                    t.other = true;
            }
        }
    t.global = t.ids.empty() || std::find(t.ids.begin(), t.ids.end(), 0) != t.ids.end();
    t.applies = !t.other && (t.global || std::find(t.ids.begin(), t.ids.end(), uid) != t.ids.end());
    return t;
}
Bytes targets(const Node &tag, const std::vector<uint64_t> &ids) {
    Node original{0x63c0, {}, {}};
    for (const auto &n : children(tag.body))
        if (n.id == 0x63c0)
            original = n;
    Bytes body;
    for (const auto &n : children(original.body))
        if (n.id != 0x63c5 && n.id != 0xbf)
            append(body, n.raw);
    for (auto uid : ids)
        append(body, number(0x63c5, uid));
    return rebuild(original, std::move(body));
}
Bytes select_simple(const Node &node, const std::function<bool(const std::wstring &)> &keep,
                    unsigned depth = 0) {
    require(depth <= 8);
    auto list = children(node.body);
    std::wstring key;
    for (const auto &n : list)
        if (n.id == 0x45a3)
            key = canonical_tag(value(n));
    const bool retain = keep(key);
    bool content{};
    Bytes body;
    for (const auto &n : list) {
        if (n.id == 0xbf)
            continue;
        if (n.id == 0x67c8) {
            auto inner = select_simple(n, keep, depth + 1);
            content |= !inner.empty();
            append(body, inner);
        } else if (n.id == 0x4487 || n.id == 0x4485) {
            if (retain) {
                append(body, n.raw);
                content = true;
            }
        } else
            append(body, n.raw);
    }
    if (!content)
        return {};
    auto edited = rebuild(node, std::move(body));
    return edited == node.raw ? node.raw : edited;
}
void simple_keys(const Node &node, std::set<std::wstring> &keys, unsigned depth = 0) {
    require(depth <= 8);
    std::wstring key;
    bool content{};
    for (const auto &n : children(node.body)) {
        if (n.id == 0x45a3)
            key = canonical_tag(value(n));
        if (n.id == 0x4487 || n.id == 0x4485)
            content = true;
        if (n.id == 0x67c8)
            simple_keys(n, keys, depth + 1);
    }
    if (content && !key.empty())
        keys.insert(key);
}
std::set<uint64_t> removed_covers(const Document &doc) {
    std::set<uint64_t> removed;
    if (doc.covers_changed)
        for (auto uid : doc.original_cover_uids)
            if (uid && std::none_of(doc.covers.begin(), doc.covers.end(),
                                    [&](const Cover &c) { return c.uid == uid; }))
                removed.insert(uid);
    return removed;
}
Bytes prune_attachment_target(const Node &tag, const std::set<uint64_t> &removed) {
    Bytes body;
    bool changed{};
    for (const auto &n : children(tag.body)) {
        if (n.id == 0xbf)
            continue;
        if (n.id != 0x63c0) {
            append(body, n.raw);
            continue;
        }
        Bytes scope;
        bool deleted{}, remaining{};
        for (const auto &t : children(n.body)) {
            if (t.id == 0xbf)
                continue;
            if (t.id == 0x63c6 && removed.count(value_number(t))) {
                deleted = true;
                continue;
            }
            remaining |= t.id == 0x63c6;
            append(scope, t.raw);
        }
        if (deleted && !remaining)
            return {};
        changed |= deleted;
        append(body, deleted ? rebuild(n, std::move(scope)) : n.raw);
    }
    return changed ? rebuild(tag, std::move(body)) : tag.raw;
}
Bytes updated_tags(Document &doc) {
    const auto removed = removed_covers(doc);
    std::map<uint64_t, std::set<std::wstring>> specific;
    for (const auto &e : doc.top)
        if (e.id == TAGS)
            for (const auto &tag : children(load(doc, e).body))
                if (tag.id == 0x7373) {
                    const auto scope = target(tag, doc.track_uid);
                    if (scope.other || scope.global)
                        continue;
                    for (auto uid : scope.ids)
                        for (const auto &n : children(tag.body))
                            if (n.id == 0x67c8)
                                simple_keys(n, specific[uid]);
                }
    Bytes output;
    const auto affected = [&](const std::wstring &key) {
        return doc.clear_tags || doc.tag_changes.count(key) != 0;
    };
    for (const auto &e : doc.top)
        if (e.id == TAGS) {
            auto root = load(doc, e);
            Bytes body;
            for (const auto &original_tag : children(root.body)) {
                auto tag = original_tag;
                if (tag.id == 0xbf)
                    continue;
                if (tag.id == 0x7373 && !removed.empty()) {
                    auto pruned = prune_attachment_target(tag, removed);
                    if (pruned.empty())
                        continue;
                    tag = std::move(children(pruned)[0]);
                }
                if (tag.id != 0x7373 || !target(tag, doc.track_uid).applies) {
                    append(body, tag.raw);
                    continue;
                }
                auto scope = target(tag, doc.track_uid);
                Bytes remaining;
                bool changed{}, has_simple{}, extra{};
                for (const auto &n : children(tag.body)) {
                    if (n.id == 0xbf)
                        continue;
                    if (n.id == 0x67c8) {
                        auto b =
                            select_simple(n, [&](const std::wstring &k) { return !affected(k); });
                        changed |= b != n.raw;
                        has_simple |= !b.empty();
                        append(remaining, b);
                    } else {
                        append(remaining, n.raw);
                        extra |= n.id != 0x63c0;
                    }
                }
                if (!changed) {
                    append(body, tag.raw);
                    continue;
                }
                // Unedited inherited fields retain their original scope and priority.
                // Only removed fields are split out for the other tracks, excluding
                // fields which those tracks already override explicitly.
                auto others = scope.global ? doc.track_uids : scope.ids;
                others.erase(std::remove(others.begin(), others.end(), doc.track_uid),
                             others.end());
                for (auto uid : others) {
                    Bytes kept;
                    for (const auto &n : children(tag.body))
                        if (n.id == 0x67c8)
                            append(kept, select_simple(n, [&](const std::wstring &k) {
                                       return affected(k) &&
                                              (!scope.global || !specific[uid].count(k));
                                   }));
                    if (!kept.empty()) {
                        auto b = targets(tag, {uid});
                        append(b, kept);
                        append(body, rebuild(tag, std::move(b)));
                    }
                }
                require(has_simple || !extra, E_NOTIMPL);
                if (has_simple)
                    append(body, rebuild(tag, std::move(remaining)));
            }
            if (!body.empty())
                append(output, rebuild(root, std::move(body)));
        }
    Bytes body;
    for (const auto &[key, v] : doc.tag_changes)
        if (!v.empty() || key == L"TITLE") {
            auto b = string(0x45a3, key);
            append(b, string(0x4487, v));
            append(body, ebml(0x67c8, b));
        }
    if (doc.clear_tags && !doc.tag_changes.count(L"TITLE")) {
        auto b = string(0x45a3, L"TITLE");
        append(b, string(0x4487, L""));
        append(body, ebml(0x67c8, b));
    }
    if (!body.empty()) {
        auto t = ebml(0x63c0, number(0x63c5, doc.track_uid));
        append(t, body);
        append(output, ebml(TAGS, ebml(0x7373, t)));
    }
    return output;
}
uint64_t next_uid(std::set<uint64_t> &used) {
    for (;;) {
        GUID guid{};
        check(CoCreateGuid(&guid));
        uint64_t id{};
        memcpy(&id, &guid, 8);
        if (id && used.insert(id).second)
            return id;
    }
}
Bytes cover_bytes(const Cover &cover, const Node *old, std::set<uint64_t> &used) {
    const auto uid = cover.uid ? cover.uid : next_uid(used);
    auto name = cover.name;
    if (name.empty()) {
        name = cover.type == 3   ? L"cover-"
               : cover.type == 4 ? L"back-"
                                 : L"ttplayer-cover-" + std::to_wstring(cover.type) + L"-";
        name += std::to_wstring(uid) + (cover.mime == L"image/png" ? L".png" : L".jpg");
        if (cover.type == 3)
            name = cover.mime == L"image/png" ? L"cover.png" : L"cover.jpg";
    }
    Bytes body;
    if (old)
        for (const auto &n : children(old->body))
            if (n.id != 0xbf && n.id != 0x466e && n.id != 0x4660 && n.id != 0x467e &&
                n.id != 0x465c && n.id != 0x46ae)
                append(body, n.raw);
    append(body, string(0x466e, name));
    append(body, string(0x4660, cover.mime));
    append(body, string(0x467e, cover.description));
    append(body, number(0x46ae, uid));
    append(body, ebml(0x465c, cover.data));
    return old ? rebuild(*old, std::move(body)) : ebml(0x61a7, body);
}
Bytes updated_attachments(Document &doc) {
    Bytes body;
    std::set<uint64_t> used;
    Node wrapper{ATTACHMENTS, {}, {}};
    bool have_wrapper{};
    for (const auto &e : doc.top)
        if (e.id == ATTACHMENTS)
            for (const auto &a : children(load(doc, e).body))
                if (a.id == 0x61a7)
                    for (const auto &n : children(a.body))
                        if (n.id == 0x46ae)
                            require(used.insert(value_number(n)).second);
    for (const auto &e : doc.top)
        if (e.id == ATTACHMENTS) {
            auto root = load(doc, e);
            if (!have_wrapper) {
                wrapper = root;
                have_wrapper = true;
            }
            uint64_t at = e.data;
            for (const auto &a : children(root.body)) {
                const auto origin = at;
                at += a.raw.size();
                if (a.id == 0xbf)
                    continue;
                auto found = std::find_if(doc.covers.begin(), doc.covers.end(),
                                          [&](const Cover &c) { return c.origin == origin; });
                if (found != doc.covers.end())
                    append(body, found->changed ? cover_bytes(*found, &a, used) : a.raw);
                else if (std::find(doc.original_cover_origins.begin(),
                                   doc.original_cover_origins.end(),
                                   origin) == doc.original_cover_origins.end())
                    append(body, a.raw); // Unrelated or unrecognized attachments are immutable.
            }
        }
    for (const auto &c : doc.covers)
        if (!c.origin)
            append(body, cover_bytes(c, nullptr, used));
    return body.empty() ? Bytes{} : rebuild(wrapper, std::move(body));
}
struct Temporary {
    std::wstring path;
    ComPtr<IStream> stream;
    bool keep{};
    explicit Temporary(const std::wstring &folder) {
        GUID g{};
        check(CoCreateGuid(&g));
        wchar_t name[64]{};
        StringFromGUID2(g, name, 64);
        path = folder + L"ttplayer-webm-" + name + L".tmp";
        check(SHCreateStreamOnFileEx(path.c_str(),
                                     STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
                                     FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, stream.put()));
    }
    ~Temporary() {
        stream.reset();
        if (!keep && !path.empty())
            DeleteFileW(path.c_str());
    }
};
BY_HANDLE_FILE_INFORMATION file_info(const std::wstring &path) {
    HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        throw Failure{HRESULT_FROM_WIN32(GetLastError())};
    BY_HANDLE_FILE_INFORMATION st{};
    BOOL ok = GetFileInformationByHandle(h, &st);
    DWORD err = GetLastError();
    CloseHandle(h);
    require(ok, HRESULT_FROM_WIN32(err));
    return st;
}
bool equal_stamp(const BY_HANDLE_FILE_INFORMATION &a, const BY_HANDLE_FILE_INFORMATION &b) {
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
           a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow &&
           a.nFileSizeHigh == b.nFileSizeHigh && a.nFileSizeLow == b.nFileSizeLow &&
           CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) == 0;
}
void copy(IStream *from, IStream *to, uint64_t size) {
    Bytes scratch(256 * 1024);
    while (size) {
        ULONG got{};
        auto n = DWORD(std::min<uint64_t>(size, scratch.size()));
        check(from->Read(scratch.data(), n, &got));
        require(got && got <= n, STG_E_READFAULT);
        write_exact(to, scratch.data(), got);
        size -= got;
    }
}
void write(IStream *to, const Bytes &bytes) {
    if (!bytes.empty())
        write_exact(to, bytes.data(), DWORD(bytes.size()));
}
void void_bytes(IStream *to, uint64_t total) {
    if (!total)
        return;
    require(total >= 2);
    unsigned width = 1;
    while (width < 8 && total - 1 - width >= ((uint64_t(1) << (7 * width)) - 1))
        ++width;
    require(total >= 1 + width);
    write(to, {0xec});
    write(to, vint(total - 1 - width, width));
    auto n = total - 1 - width;
    Bytes zeros(65536);
    while (n) {
        auto count = DWORD(std::min<uint64_t>(n, zeros.size()));
        write_exact(to, zeros.data(), count);
        n -= count;
    }
}
Bytes seek_entry(uint64_t id, uint64_t position) {
    auto b = ebml(0x53ab, integer(id));
    append(b, number(0x53ac, position));
    return ebml(0x4dbb, b);
}
void recovery(Temporary &backup, Temporary &next) {
    backup.keep = next.keep = true;
    OutputDebugStringW((L"ttp_webm: original recovery copy: " + backup.path + L"; edited copy: " +
                        next.path + L"\n")
                           .c_str());
}
} // namespace

std::wstring canonical_tag(std::wstring key) {
    for (auto &c : key)
        if (c >= L'a' && c <= L'z')
            c -= 32;
    if (key == L"YEAR" || key == L"DATE")
        return L"DATE_RELEASED";
    if (key == L"TRACK" || key == L"TRACKNUMBER")
        return L"PART_NUMBER";
    if (key == L"ALBUMARTIST")
        return L"ALBUM_ARTIST";
    return key;
}
void Document::capture_file() {
    stamped = false;
    protect([&]() -> HRESULT {
        STATSTG st{};
        check(source.stream()->Stat(&st, STATFLAG_DEFAULT));
        stream_mode = st.grfMode;
        std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> name(st.pwcsName, CoTaskMemFree);
        require(name && *name.get() && std::wstring(name.get()).find(L"://") == std::wstring::npos,
                STG_E_ACCESSDENIED);
        wchar_t full[32768]{};
        auto n = GetFullPathNameW(name.get(), 32768, full, nullptr);
        require(n && n < 32768);
        path = full;
        stamp = file_info(path);
        require((uint64_t(stamp.nFileSizeHigh) << 32 | stamp.nFileSizeLow) == source.size);
        stamped = true;
        return S_OK;
    });
}
bool Document::can_edit() const {
    if (!safe_layout || !stamped || path.empty())
        return false;
    auto a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES &&
           !(a & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY));
}
void Document::set_tag(const char *key, const wchar_t *value_) {
    require(can_edit(), STG_E_ACCESSDENIED);
    if (!key) {
        require(!value_, E_INVALIDARG);
        tags.clear();
        tag_changes.clear();
        clear_tags = true;
        return;
    }
    auto k = canonical_tag(wide(key));
    require(!k.empty() && k.size() <= 1024, E_INVALIDARG);
    for (auto c : k)
        require(c >= 32 && c < 127 && c != L'=', E_INVALIDARG);
    std::wstring v = value_ ? value_ : L"";
    require(utf8(v).size() <= 1024 * 1024, E_INVALIDARG);
    auto updated = tags;
    updated.erase(std::remove_if(updated.begin(), updated.end(),
                                 [&](const Tag &t) { return canonical_tag(t.key) == k; }),
                  updated.end());
    if (!v.empty()) {
        auto display = k == L"DATE_RELEASED"  ? L"Year"
                       : k == L"PART_NUMBER"  ? L"Track"
                       : k == L"ALBUM_ARTIST" ? L"AlbumArtist"
                                              : k;
        updated.push_back({display, v, true});
    }
    require(updated.size() <= 4096, E_INVALIDARG);
    auto changes = tag_changes;
    changes[k] = std::move(v);
    tags.swap(updated);
    tag_changes.swap(changes);
}
Cover edited_cover(const Picture &p) {
    require(p.size >= sizeof(Picture) && p.type <= 20 && p.bytes && p.bytes <= 16 * 1024 * 1024 &&
                p.data,
            E_INVALIDARG);
    Cover c;
    c.type = p.type;
    c.description = p.description ? p.description : L"";
    require(utf8(c.description).size() <= 1024 * 1024, E_INVALIDARG);
    c.data.assign(p.data, p.data + p.bytes);
    const auto &b = c.data;
    if (b.size() >= 33 && !memcmp(b.data(), "\x89PNG\r\n\x1a\n", 8)) {
        require(be32(b.data() + 8) == 13 && !memcmp(b.data() + 12, "IHDR", 4) &&
                    be32(b.data() + 16) && be32(b.data() + 20),
                E_INVALIDARG);
        bool end{};
        for (size_t at = 8; at < b.size();) {
            require(b.size() - at >= 12, E_INVALIDARG);
            auto n = be32(b.data() + at);
            require(n <= b.size() - at - 12, E_INVALIDARG);
            require((crc_update(0xffffffff, b.data() + at + 4, n + 4) ^ 0xffffffff) ==
                        be32(b.data() + at + 8 + n),
                    E_INVALIDARG);
            if (!memcmp(b.data() + at + 4, "IEND", 4)) {
                require(n == 0 && at + 12 == b.size(), E_INVALIDARG);
                end = true;
            }
            at += n + 12;
        }
        require(end, E_INVALIDARG);
        c.mime = L"image/png";
    } else if (b.size() >= 4 && b[0] == 255 && b[1] == 216 && b[b.size() - 2] == 255 &&
               b.back() == 217) {
        size_t at = 2;
        bool frame{};
        while (at + 1 < b.size()) {
            require(b[at++] == 255, E_INVALIDARG);
            while (at < b.size() && b[at] == 255)
                ++at;
            require(at < b.size(), E_INVALIDARG);
            auto m = b[at++];
            if (m == 218 || m == 217)
                break;
            if (m == 1 || (m >= 208 && m <= 215))
                continue;
            require(b.size() - at >= 2, E_INVALIDARG);
            auto n = be16(b.data() + at);
            require(n >= 2 && n <= b.size() - at, E_INVALIDARG);
            if ((m >= 192 && m <= 195) || (m >= 197 && m <= 199) || (m >= 201 && m <= 203) ||
                (m >= 205 && m <= 207)) {
                require(n >= 8 && be16(b.data() + at + 3) && be16(b.data() + at + 5), E_INVALIDARG);
                frame = true;
            }
            at += n;
        }
        require(frame, E_INVALIDARG);
        c.mime = L"image/jpeg";
    } else
        throw Failure{E_INVALIDARG};
    require(!p.mime || !*p.mime || !_wcsicmp(p.mime, c.mime.c_str()) ||
                (c.mime == L"image/jpeg" && !_wcsicmp(p.mime, L"image/jpg")),
            E_INVALIDARG);
    c.changed = true;
    return c;
}
HRESULT Document::save() noexcept {
    return protect([&]() -> HRESULT {
        if (!dirty())
            return S_OK;
        require(can_edit(), STG_E_ACCESSDENIED);
        require(source.stream(), E_UNEXPECTED);
        const auto check_current = [&]() {
            require(equal_stamp(stamp, file_info(path)), HRESULT_FROM_WIN32(ERROR_FILE_INVALID));
        };
        check_current();
        require(stream_size(source.stream()) == source.size,
                HRESULT_FROM_WIN32(ERROR_FILE_INVALID));
        const bool edit_tags = clear_tags || !tag_changes.empty() || !removed_covers(*this).empty();
        auto tags_out = edit_tags ? updated_tags(*this) : Bytes{};
        auto covers_out = covers_changed ? updated_attachments(*this) : Bytes{};
        const auto retired = [&](const Element &e) {
            return e.id == SEEKHEAD || (edit_tags && e.id == TAGS) ||
                   (covers_changed && e.id == ATTACHMENTS);
        };
        uint64_t kept_end = segment_end;
        for (auto i = top.rbegin(); i != top.rend() && (retired(*i) || i->id == 0xec); ++i)
            kept_end = i->begin;
        uint64_t payload = kept_end - segment_data;
        Bytes entries;
        for (const auto &e : top) {
            require(e.id != 0x1b538667,
                    E_NOTIMPL); // Signed layouts require their signer, not blind copying.
            if (e.end <= kept_end && e.id != 0xec && e.id != 0xbf && e.id != 0x1f43b675 &&
                !retired(e))
                append(entries, seek_entry(e.id, e.begin - segment_data));
        }
        for (const auto *added : {&tags_out, &covers_out}) {
            for (const auto &n : children(*added)) {
                append(entries, seek_entry(n.id, payload));
                payload += n.raw.size();
            }
        }
        const uint64_t seek_at = payload;
        const auto head = ebml(SEEKHEAD, entries);
        payload += head.size();
        require(payload < ((uint64_t(1) << 56) - 1));
        const auto pointer = ebml(SEEKHEAD, seek_entry(SEEKHEAD, seek_at));
        const Element *root{};
        for (const auto &e : top)
            if (e.end <= kept_end && e.id == SEEKHEAD && e.end - e.begin >= pointer.size() &&
                e.end - e.begin - pointer.size() != 1) {
                root = &e;
                break;
            }
        if (!root)
            for (const auto &e : top)
                if (e.end <= kept_end && e.id == 0xec && e.end - e.begin >= pointer.size() &&
                    e.end - e.begin - pointer.size() != 1) {
                    root = &e;
                    break;
                }
        Temporary next(path.substr(0, path.find_last_of(L"\\/") + 1));
        seek(source.stream(), 0);
        copy(source.stream(), next.stream.p, segment_header);
        unsigned width = unsigned(segment_data - segment_header - 4);
        require(width >= 1 && width <= 8);
        if (payload >= ((uint64_t(1) << (width * 7)) - 1))
            width = 8;
        write(next.stream.p, integer(0x18538067));
        write(next.stream.p, vint(payload, width));
        const auto new_data = tell(next.stream.p);
        const Element *crc{};
        for (const auto &e : top) {
            if (e.begin >= kept_end)
                break;
            require(tell(next.stream.p) - new_data == e.begin - segment_data);
            if (e.id == 0xbf) {
                require(!crc && e.end - e.data == 4);
                crc = &e;
            }
            if (&e == root) {
                write(next.stream.p, pointer);
                void_bytes(next.stream.p, e.end - e.begin - pointer.size());
            } else if (e.id == SEEKHEAD || (edit_tags && e.id == TAGS) ||
                       (covers_changed && e.id == ATTACHMENTS))
                void_bytes(next.stream.p, e.end - e.begin);
            else if (e.id == 0x1f43b675 && e.unknown) {
                // Close the old final Cluster before appending metadata. Keep
                // its existing size-field width, so block/seek offsets do not move.
                const auto width = unsigned(e.data - e.begin - 4);
                const auto bytes = e.end - e.data;
                require(width >= 1 && width <= 8 && bytes < ((uint64_t(1) << (width * 7)) - 1),
                        E_NOTIMPL);
                write(next.stream.p, integer(e.id));
                write(next.stream.p, vint(bytes, width));
                seek(source.stream(), e.data);
                copy(source.stream(), next.stream.p, bytes);
            } else {
                seek(source.stream(), e.begin);
                copy(source.stream(), next.stream.p, e.end - e.begin);
            }
        }
        write(next.stream.p, tags_out);
        write(next.stream.p, covers_out);
        write(next.stream.p, head);
        const auto new_size = tell(next.stream.p);
        require(new_size - new_data == payload);
        if (crc) {
            DWORD value = 0xffffffff;
            Bytes buffer(256 * 1024);
            const auto accumulate = [&](uint64_t begin, uint64_t end) {
                seek(next.stream.p, begin);
                while (begin < end) {
                    auto n = DWORD(std::min<uint64_t>(buffer.size(), end - begin));
                    read_exact(next.stream.p, buffer.data(), n);
                    value = crc_update(value, buffer.data(), n);
                    begin += n;
                }
            };
            accumulate(new_data, new_data + crc->begin - segment_data);
            accumulate(new_data + crc->end - segment_data, new_size);
            seek(next.stream.p, new_data + crc->data - segment_data);
            write(next.stream.p, crc_bytes(value ^ 0xffffffff));
        }
        check(next.stream->Commit(STGC_DEFAULT));
        // Reparse before touching the destination. Container edits must preserve
        // the selected audio timeline and the requested metadata view.
        {
            Document verify;
            verify.open(next.stream.p);
            require(verify.frames == frames && verify.codec.channels == codec.channels &&
                    verify.codec.rate == codec.rate && verify.codec.opus == codec.opus &&
                    verify.packets.size() == packets.size());
            const auto view = [](const std::vector<Tag> &input) {
                std::vector<std::pair<std::wstring, std::wstring>> out;
                for (const auto &t : input)
                    out.emplace_back(canonical_tag(t.key), t.value);
                std::sort(out.begin(), out.end());
                return out;
            };
            require(view(verify.tags) == view(tags));
            require(verify.covers.size() == covers.size());
            std::vector<bool> matched(verify.covers.size());
            for (const auto &c : covers) {
                bool found{};
                for (size_t i = 0; i < verify.covers.size(); ++i) {
                    const auto &v = verify.covers[i];
                    if (!matched[i] && c.type == v.type && c.mime == v.mime &&
                        c.description == v.description && c.data == v.data) {
                        matched[i] = true;
                        found = true;
                        break;
                    }
                }
                require(found);
            }
        }
        check_current();
        Temporary backup(path.substr(0, path.find_last_of(L"\\/") + 1));
        seek(source.stream(), 0);
        copy(source.stream(), backup.stream.p, source.size);
        check(backup.stream->Commit(STGC_DEFAULT));
        check_current();
        ComPtr<IStream> writable;
        if ((stream_mode & 3) == STGM_READWRITE)
            writable = ComPtr<IStream>(source.stream(), true);
        source.close();
        next.stream.reset();
        backup.stream.reset();
        if (!writable) {
            if (ReplaceFileW(path.c_str(), next.path.c_str(), nullptr, 0, nullptr, nullptr))
                return S_OK;
            // Never write through a path whose identity changed during a failed replace.
            if (FAILED(protect([&]() -> HRESULT {
                    check_current();
                    return S_OK;
                }))) {
                recovery(backup, next);
                return HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
            }
            check(SHCreateStreamOnFileEx(path.c_str(), STGM_READWRITE | STGM_SHARE_DENY_WRITE, 0,
                                         FALSE, nullptr, writable.put()));
        }
        check_current();
        check(SHCreateStreamOnFileEx(next.path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE,
                                     nullptr, next.stream.put()));
        check(SHCreateStreamOnFileEx(backup.path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0,
                                     FALSE, nullptr, backup.stream.put()));
        const auto overwrite = [&](IStream *from, uint64_t bytes) {
            seek(from, 0);
            seek(writable.p, 0);
            copy(from, writable.p, bytes);
            ULARGE_INTEGER size{};
            size.QuadPart = bytes;
            check(writable->SetSize(size));
            check(writable->Commit(STGC_DEFAULT));
        };
        auto hr = protect([&]() -> HRESULT {
            overwrite(next.stream.p, new_size);
            return S_OK;
        });
        if (FAILED(hr)) {
            auto rollback = protect([&]() -> HRESULT {
                overwrite(backup.stream.p, source.size);
                return S_OK;
            });
            if (FAILED(rollback))
                recovery(backup, next);
        }
        return hr;
    });
}
} // namespace ttp::webm
