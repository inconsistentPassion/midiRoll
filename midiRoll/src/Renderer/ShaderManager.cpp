#include "ShaderManager.h"
#include <d3dcompiler.h>
#include <stdexcept>

namespace pfd {

#ifdef _DEBUG
static constexpr UINT kShaderCompileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
static constexpr UINT kShaderCompileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

ShaderManager::ShaderManager(ID3D11Device* device) : m_device(device) {}

bool ShaderManager::LoadShader(const std::string& name, const std::wstring& path,
                                const std::string& vsEntry, const std::string& psEntry) {
    ShaderSet set;

    // Compile VS
    ComPtr<ID3DBlob> vsBlob, errBlob;
    HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        vsEntry.c_str(), "vs_5_0", kShaderCompileFlags, 0,
        vsBlob.GetAddressOf(), errBlob.GetAddressOf());
    if (FAILED(hr)) {
        if (errBlob) OutputDebugStringA((char*)errBlob->GetBufferPointer());
        return false;
    }

    hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
        nullptr, set.vs.GetAddressOf());
    if (FAILED(hr)) return false;
    set.vsBlob = vsBlob;

    // Compile PS
    ComPtr<ID3DBlob> psBlob;
    hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        psEntry.c_str(), "ps_5_0", kShaderCompileFlags, 0,
        psBlob.GetAddressOf(), errBlob.GetAddressOf());
    if (FAILED(hr)) {
        if (errBlob) OutputDebugStringA((char*)errBlob->GetBufferPointer());
        return false;
    }

    hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
        nullptr, set.ps.GetAddressOf());
    if (FAILED(hr)) return false;

    m_shaders[name] = std::move(set);
    return true;
}

bool ShaderManager::LoadComputeShader(const std::string& name, const std::wstring& path,
                                       const std::string& csEntry) {
    ShaderSet set;
    ComPtr<ID3DBlob> csBlob, errBlob;
    HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        csEntry.c_str(), "cs_5_0", kShaderCompileFlags, 0,
        csBlob.GetAddressOf(), errBlob.GetAddressOf());
    if (FAILED(hr)) {
        if (errBlob) OutputDebugStringA((char*)errBlob->GetBufferPointer());
        return false;
    }

    hr = m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(),
        nullptr, set.cs.GetAddressOf());
    if (FAILED(hr)) return false;

    m_shaders[name] = std::move(set);
    return true;
}

ID3D11VertexShader* ShaderManager::GetVS(const std::string& name) const {
    auto it = m_shaders.find(name);
    return it != m_shaders.end() ? it->second.vs.Get() : nullptr;
}

ID3D11PixelShader* ShaderManager::GetPS(const std::string& name) const {
    auto it = m_shaders.find(name);
    return it != m_shaders.end() ? it->second.ps.Get() : nullptr;
}

ID3D11ComputeShader* ShaderManager::GetCS(const std::string& name) const {
    auto it = m_shaders.find(name);
    return it != m_shaders.end() ? it->second.cs.Get() : nullptr;
}

ID3D11InputLayout* ShaderManager::GetLayout(const std::string& name) const {
    auto it = m_shaders.find(name);
    return it != m_shaders.end() ? it->second.layout.Get() : nullptr;
}

void ShaderManager::Bind(const std::string& name, ID3D11DeviceContext* ctx) const {
    auto it = m_shaders.find(name);
    if (it == m_shaders.end()) return;
    ctx->VSSetShader(it->second.vs.Get(), nullptr, 0);
    ctx->PSSetShader(it->second.ps.Get(), nullptr, 0);
    if (it->second.layout)
        ctx->IASetInputLayout(it->second.layout.Get());
}

} // namespace pfd
