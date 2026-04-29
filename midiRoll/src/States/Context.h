#pragma once
#include "../Window.h"
#include "../Renderer/D3DContext.h"
#include "../Renderer/SpriteBatch.h"
#include "../Renderer/FontRenderer.h"
#include "../Piano/PianoRenderer.h"
#include "../Piano/NoteState.h"
#include "../Audio/SoundFontEngine.h"
#include "../Audio/MidiParser.h"
#include "../Input.h"
#include "../Util/Timer.h"

namespace pfd {

// Shared resources passed to all states
struct Context {
    Window*          window{};
    D3DContext*       d3d{};
    SpriteBatch*     spriteBatch{};
    FontRenderer*    font{};
    PianoRenderer*   piano{};
    NoteState*       noteState{};
    SoundFontEngine* audio{};
    MidiParser*      midi{};
    Input*           input{};
    util::Timer*     timer{};

    // Paths (persisted across states)
    std::string      midiFilePath;
    std::string      soundFontPath;
    bool             midiLoaded{};
};

} // namespace pfd
