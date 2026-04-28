#pragma once
#include "D3DContext.h"
#include <string>
#include <unordered_map>

namespace pfd {

class ShaderManager {
public:
    explicit ShaderManager(ID3D11Device* device);

    // Compile and create vertex + pixel shader from .hlsl file
    bool LoadShader(const std::string& name, const std::wstring& path,
                    const std::string& vsEntry = "VSMain",
                    const std::string& psEntry = "PSMain");

    // Compile compute shader
    bool LoadComputeShader(const std::string& name, const std::wstring& path,
                           const std::string& csEntry = "CSMain");

    ID3D11VertexShader*  GetVS(const std::string& name) const;
    ID3D11PixelShader*   GetPS(const std::string& name) const;
    ID3D11ComputeShader* GetCS(const std::string& name) const;
    ID3D11InputLayout*   GetLayout(const std::string& name) const;

    void Bind(const std::string& name, ID3D11DeviceContext* ctx) const;

private:
    struct ShaderSet {
        ComPtr<ID3D11VertexShader>  vs;
        ComPtr<ID3D11PixelShader>   ps;
        ComPtr<ID3D11ComputeShader> cs;
        ComPtr<ID3D11InputLayout>   layout;
        ComPtr<ID3DBlob>            vsBlob;
    };

    ID3D11Device* m_device;
    std::unordered_map<std::string, ShaderSet> m_shaders;
};

} // namespace pfd
