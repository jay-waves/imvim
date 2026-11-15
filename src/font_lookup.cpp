#include "font_lookup.hpp"
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#else
#include "process.hpp"
#include <sstream>
#endif
namespace imvim {
FontFace find_system_font(const std::string& family) {
    if (family.empty()) throw std::runtime_error("Font family cannot be empty");
#ifdef _WIN32
    using Microsoft::WRL::ComPtr;
    auto check = [&](HRESULT result) {
        if (FAILED(result)) throw std::runtime_error("Cannot resolve system font: " + family);
    };
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, family.data(), static_cast<int>(family.size()), nullptr, 0);
    if (!n) throw std::runtime_error("Invalid UTF-8 font family");
    std::wstring name(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, family.data(), static_cast<int>(family.size()), name.data(), n);
    ComPtr<IDWriteFactory> factory;
    check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(factory.GetAddressOf())));
    ComPtr<IDWriteFontCollection> collection;
    check(factory->GetSystemFontCollection(&collection));
    UINT32 index = 0; BOOL exists = FALSE;
    check(collection->FindFamilyName(name.c_str(), &index, &exists));
    if (!exists) throw std::runtime_error("System font family not installed: " + family);
    ComPtr<IDWriteFontFamily> font_family;
    check(collection->GetFontFamily(index, &font_family));
    ComPtr<IDWriteFont> font;
    check(font_family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font));
    ComPtr<IDWriteFontFace> face;
    check(font->CreateFontFace(&face));
    UINT32 count = 0;
    check(face->GetFiles(&count, nullptr));
    if (count != 1) throw std::runtime_error("Unsupported multi-file font: " + family);
    ComPtr<IDWriteFontFile> file;
    check(face->GetFiles(&count, file.GetAddressOf()));
    ComPtr<IDWriteFontFileLoader> loader;
    check(file->GetLoader(&loader));
    ComPtr<IDWriteLocalFontFileLoader> local;
    check(loader.As(&local));
    const void* key = nullptr; UINT32 key_size = 0;
    check(file->GetReferenceKey(&key, &key_size));
    UINT32 length = 0;
    check(local->GetFilePathLengthFromKey(key, key_size, &length));
    std::wstring path(length + 1, 0);
    check(local->GetFilePathFromKey(key, key_size, path.data(), length + 1));
    path.resize(length);
    return {std::filesystem::path(path), static_cast<int>(face->GetIndex())};
#else
    // Query the installed Fontconfig tool directly; arguments never pass through a shell.
    Process query("fc-match", {"-f", "%{file}\n%{index}\n", "--", family});
    std::string output; char buffer[4096];
    while (auto size = query.read(buffer, sizeof(buffer))) {
        output.append(buffer, size);
        if (output.size() > 65536) throw std::runtime_error("Unexpected fc-match response");
    }
    std::istringstream stream(output); std::string path, index;
    if (!std::getline(stream,path) || !std::getline(stream,index) || path.empty())
        throw std::runtime_error("Cannot resolve system font using fc-match: " + family);
    int face_index = std::stoi(index);
    if (face_index < 0 || face_index > 65535) throw std::runtime_error("Variable font named instances are not yet supported");
    return {std::filesystem::path(std::u8string(path.begin(),path.end())), face_index};
#endif
}
}
