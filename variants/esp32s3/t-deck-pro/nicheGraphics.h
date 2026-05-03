#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/msgui/MsgUI.h"

// Called from main.cpp after modules are set up.
// Ownership of the display lifecycle belongs entirely to MsgUI from this point on.
void setupNicheGraphics()
{
    msgui::setup();
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
