#pragma once
#include "D3DContext.h"
#include "../Util/Math.h"
#include "../Util/Color.h"
#include <vector>

namespace pfd {

// GPU vertex for a quad corner
struct SpriteVertex {
    util::Vec2 position;
    util::Vec2 uv;
    util::Vec4 color;
};

// CPU-side instance data (one per quad)
struct SpriteInstance {
    util::Vec2 position;  // top-left in pixels
    util::Vec2 size;      // width, height in pixels
    util::Vec4 color;     // RGBA
    util::Vec2 uvOrigin;  // UV top-left (0,0 default)
    util::Vec2 uvSize;    // UV size (1,1 default)
};

class SpriteBatch {
public:
    bool Initialize(ID3D11Device* device);
    void Begin(ID3D11DeviceContext* ctx);
    void Draw(util::Vec2 pos, util::Vec2 size, util::Vec4 color,
              util::Vec2 uvOrigin = {0,0}, util::Vec2 uvSize = {1,1});
    void DrawGradientV(util::Vec2 pos, util::Vec2 size,
                       util::Vec4 topColor, util::Vec4 bottomColor);
    void End(ID3D11DeviceContext* ctx, uint32_t viewWidth, uint32_t viewHeight);

    size_t QuadCount() const { return m_instances.size(); }

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
    ComPtr<ID3D11ShaderResourceView>  m_whiteTexture;

    std::vector<SpriteInstance> m_instances;
    size_t m_maxInstances = 16384;
};

} // namespace pfd
