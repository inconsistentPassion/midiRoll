#pragma once
#include <d3d11.h>
#include <string>
#include <wrl/client.h>

namespace pfd {
namespace util {

using Microsoft::WRL::ComPtr;

class TextureLoader {
public:
    static ComPtr<ID3D11ShaderResourceView> LoadTextureFromFile(
        ID3D11Device* device,
        const std::wstring& path
    );
};

} // namespace util
} // namespace pfd
