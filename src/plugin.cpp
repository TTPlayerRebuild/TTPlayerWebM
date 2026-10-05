#include "common.h"

namespace ttp::webm {
HMODULE module{};
class ReaderFactory final : public ReaderCreator {
    LONG references_{1};

  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!same(iid, IID_IUnknown) && !same(iid, cat_reader))
            return E_NOINTERFACE;
        *out = static_cast<ReaderCreator *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto n = InterlockedDecrement(&references_);
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Create(void **out) override { return make_reader(out); }
    HRESULT STDMETHODCALLTYPE Name(wchar_t **out) override { return text(L"WebM Reader", out); }
    HRESULT STDMETHODCALLTYPE Extensions(wchar_t **out) override {
        return text(L"WebM 音频文件(*.webm;*.weba)", out);
    }
};
class SoundAddIn final : public AddIn {
    LONG references_{1};

  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!same(iid, IID_IUnknown) && !same(iid, iid_addin))
            return E_NOINTERFACE;
        *out = static_cast<AddIn *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto n = InterlockedDecrement(&references_);
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Enum(DWORD index, GUID *category, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!category)
            return E_POINTER;
        if (index)
            return E_INVALIDARG;
        return protect([&]() -> HRESULT {
            *out = static_cast<ReaderCreator *>(new ReaderFactory);
            *category = cat_reader;
            return S_OK;
        });
    }
};
} // namespace ttp::webm
extern "C" HRESULT WINAPI ttpGetSoundAddIn(void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return ttp::webm::protect([&]() -> HRESULT {
        *out = static_cast<ttp::webm::AddIn *>(new ttp::webm::SoundAddIn);
        return S_OK;
    });
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        ttp::webm::module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
