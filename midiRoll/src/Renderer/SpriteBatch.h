#pragma once
#include "D3DContext.h"
#include "../Util/Math.h"
#include "../Util/Color.h"
#include <vector>

namespace pfd {

struct SpriteVertex {
    util::Vec2 position;
    util::Vec2 uv;
    util::Vec4 color;
};

struct SpriteInstance {
    util::Vec2 position;
    util::Vec2 size;
    util::Vec4 color;
    util::Vec2 uvOrigin;
    util::Vec2 uvSize;
};

class SpriteBatch {
public:
    bool Initialize(ID3D11Device* device);
    
    // Setup for a new frame/pass
    void Begin(ID3D11DeviceContext* ctx, uint32_t viewWidth, uint32_t viewHeight);
    
    // Draw using currently set texture
    void Draw(util::Vec2 pos, util::Vec2 size, util::Vec4 color,
              util::Vec2 uvOrigin = {0,0}, util::Vec2 uvSize = {1,1});
    
    // Draw with a specific texture (automatically flushes if texture changes)
    void Draw(util::Vec2 pos, util::Vec2 size, ID3D11ShaderResourceView* srv,
              util::Vec2 uvOrigin, util::Vec2 uvEnd, util::Vec4 color);
              
    void DrawGradientV(util::Vec2 pos, util::Vec2 size,
                       util::Vec4 topColor, util::Vec4 bottomColor);
                       
    // Finalize rendering
    void End();

    // Force current batch to be rendered immediately
    void Flush();
    
    void SetTexture(ID3D11ShaderResourceView* srv);

private:
    struct CBPerFrame {
        float viewWidth;
        float viewHeight;
        float pad[2];
    };

    ComPtr<ID3D11Buffer>              m_instanceBuffer;
    ComPtr<ID3D11Buffer>              m_vertexBuffer;
    ComPtr<ID3D11Buffer>              m_indexBuffer;
    ComPtr<ID3D11Buffer>              m_cbPerFrame;
    ComPtr<ID3D11VertexShader>        m_vs;
    ComPtr<ID3D11PixelShader>         m_ps;
    ComPtr<ID3D11InputLayout>         m_layout;
    ComPtr<ID3D11BlendState>          m_blendState;
    ComPtr<ID3D11RasterizerState>     m_rasterizerState;
    ComPtr<ID3D11DepthStencilState>   m_depthStencilState;
    ComPtr<ID3D11SamplerState>        m_samplerState;
    ComPtr<ID3D11ShaderResourceView>  m_whiteTexture;
    
    ID3D11DeviceContext*      m_ctx{};
    ID3D11ShaderResourceView* m_currentTex{};
    uint32_t m_viewW{}, m_viewH{};

    std::vector<SpriteInstance> m_instances;
    size_t m_maxInstances = 16384;
};

} // namespace pfd
