#pragma once
// ═════════════════════════════════════════════════════════════════════════════
// Frosted Glass Theme — translucent depth-based UI material system
// Adapted for D3D11 / HLSL
// ═════════════════════════════════════════════════════════════════════════════

#include "../Util/Math.h"
#include <cmath>

namespace pfd {
namespace glass {

// ─── Theme constants ────────────────────────────────────────────────────────

struct Theme {
    // Background
    util::Vec4 bgColor{0.031f, 0.031f, 0.039f, 1.0f}; // near-black, never pure #000

    // Glass material
    float glassAlpha            = 0.07f;
    float glassHoverAlpha       = 0.11f;
    float glassActiveAlpha      = 0.17f;
    float glassBorderAlpha      = 0.12f;
    float glassBorderHoverAlpha = 0.22f;
    float specularAlpha         = 0.07f;
    float specularAlphaLow      = 0.025f;
    float saturation            = 1.8f;
    float blurLOD               = 3.0f;   // mipmap level for standard glass
    float blurLODLight          = 1.5f;   // mipmap level for light glass

    // Shadows
    float shadowAlpha      = 0.18f;
    float shadowBlurRadius = 32.0f;
    float shadowOffsetY    = 8.0f;

    // Radii (matched to element size)
    float radiusSm   = 12.0f;  // small buttons, chips, tags
    float radiusMd   = 16.0f;  // cards, list items, inputs
    float radiusLg   = 20.0f;  // panels, sheets, settings groups
    float radiusXl   = 28.0f;  // large cards, notifications
    float radius2Xl  = 36.0f;  // full panels, media player
    float radiusPill = 100.0f; // nav bars, tab bars, pill buttons

    // Text hierarchy
    util::Vec4 textPrimary{0.96f, 0.96f, 0.97f, 1.0f};
    util::Vec4 textSecondary{1.0f, 1.0f, 1.0f, 0.55f};
    util::Vec4 textMuted{1.0f, 1.0f, 1.0f, 0.30f};
    util::Vec4 textDisabled{1.0f, 1.0f, 1.0f, 0.15f};

    // Accents
    util::Vec4 accentBlue{0.04f, 0.52f, 1.0f, 1.0f};
    util::Vec4 accentGreen{0.19f, 0.82f, 0.35f, 1.0f};
    util::Vec4 accentRed{1.0f, 0.22f, 0.37f, 1.0f};
    util::Vec4 accentOrange{1.0f, 0.62f, 0.04f, 1.0f};
    util::Vec4 accentPurple{0.75f, 0.35f, 0.95f, 1.0f};
    util::Vec4 accentIndigo{0.37f, 0.36f, 0.90f, 1.0f};

    // Toggle switch
    float toggleOffAlpha   = 0.15f;
    util::Vec4 toggleOnColor{0.20f, 0.78f, 0.35f, 0.70f};

    // Settings rows
    float rowAlpha          = 0.03f;
    float rowHoverAlpha     = 0.05f;
    float rowSeparatorAlpha = 0.04f;

    // Spacing
    float spaceXs  = 4.0f;
    float spaceSm  = 8.0f;
    float spaceMd  = 16.0f;
    float spaceLg  = 24.0f;
    float spaceXl  = 36.0f;
    float space2Xl = 48.0f;
    float space3Xl = 80.0f;

    // Typography sizes
    float fontH1       = 32.0f;
    float fontH2       = 24.0f;
    float fontH3       = 17.0f;
    float fontBody     = 15.0f;
    float fontCaption  = 13.0f;
    float fontMicro    = 11.0f;
    float fontTabLabel = 9.0f;

    // Animation durations (seconds)
    float hoverDuration  = 0.25f;
    float pressDuration  = 0.10f;
    float revealDuration = 0.60f;
    float staggerDelay   = 0.06f;
    float toggleDuration = 0.30f;

    // Animation transforms
    float hoverLiftY  = -1.0f;  // pixels upward on hover
    float pressScale  = 0.97f;  // scale down on press
    float revealSlideY = 24.0f; // pixels to slide up on reveal
};

// Singleton accessor
inline Theme& GetTheme() {
    static Theme t;
    return t;
}

// ─── Animation state per component ──────────────────────────────────────────

struct AnimState {
    float hoverT  = 0.0f; // 0..1, lerps toward 1 on hover
    float pressT  = 0.0f; // 0..1, lerps toward 1 on press
    float revealT = 0.0f; // 0..1, lerps from 0 to 1 on first appearance

    // Smooth toward target with frame-rate independence
    static float SmoothTo(float current, float target, float dt, float speed = 8.0f) {
        float diff = target - current;
        if (std::fabsf(diff) < 0.001f) return target;
        return current + diff * (1.0f - std::expf(-speed * dt));
    }

    void UpdateHover(bool hovered, float dt) {
        hoverT = SmoothTo(hoverT, hovered ? 1.0f : 0.0f, dt, 10.0f);
    }

    void UpdatePress(bool pressed, float dt) {
        pressT = SmoothTo(pressT, pressed ? 1.0f : 0.0f, dt, 15.0f);
    }

    void Reveal(float dt) {
        revealT = SmoothTo(revealT, 1.0f, dt, 6.0f);
    }
};

// ─── Background blob configuration ─────────────────────────────────────────

struct BackgroundBlob {
    float baseX, baseY;       // normalized 0-1 screen position
    float radius;             // fraction of screen height
    float color[4];           // RGBA
    float speedX, speedY;     // oscillation speed
    float phase;              // unique per blob

    // Compute animated position
    void GetAnimatedPos(float time, float& outX, float& outY) const {
        outX = baseX + std::sinf(time * speedX + phase) * 0.03f;
        outY = baseY + std::cosf(time * speedY + phase) * 0.02f;
    }
};

// Recommended blob configuration
inline BackgroundBlob* GetDefaultBlobs() {
    static BackgroundBlob blobs[] = {
        // Purple — top left
        {-0.05f, -0.10f, 0.30f, {0.37f, 0.36f, 0.90f, 0.55f}, 0.4f, 0.3f, 0.0f},
        // Pink — top right
        { 1.08f,  0.15f, 0.25f, {1.00f, 0.22f, 0.37f, 0.50f}, -0.35f, 0.45f, 1.5f},
        // Blue — mid left
        { 0.08f,  0.65f, 0.22f, {0.04f, 0.52f, 1.00f, 0.50f}, 0.28f, -0.38f, 3.0f},
        // Orange — bottom right
        { 0.82f,  0.78f, 0.28f, {1.00f, 0.62f, 0.04f, 0.42f}, -0.22f, -0.42f, 4.5f},
        // Green — center
        { 0.35f,  0.42f, 0.20f, {0.19f, 0.82f, 0.35f, 0.32f}, 0.25f, 0.20f, 6.0f},
        // Violet — mid right
        { 0.65f,  0.20f, 0.18f, {0.75f, 0.35f, 0.95f, 0.38f}, -0.30f, 0.25f, 2.0f},
    };
    return blobs;
}

inline constexpr int BLOB_COUNT = 6;

// ─── Button tiers ───────────────────────────────────────────────────────────

enum class ButtonTier {
    Primary, // accent-colored tinted glass
    Glass,   // standard glass
    Ghost,   // transparent with subtle border
};

// ─── Glass rendering types ──────────────────────────────────────────────────

enum class GlassType {
    Standard,  // blurLOD 3.0 — panels, cards
    Light,     // blurLOD 1.5 — tooltips, small popups
};

} // namespace glass
} // namespace pfd
