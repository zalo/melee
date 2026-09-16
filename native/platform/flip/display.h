#pragma once
void MeleeFlipInitDisplay();
// Render size Aurora draws at (GC native 4:3); the present worker scales/rotates it to the panel.
extern "C" void MeleeFlipDisplaySize(unsigned* width, unsigned* height);
// True when the SDL/KMSDRM display path is selected (the default on every device); false when
// MELEE_FLIP_DISPLAY=drm selects the legacy direct DRM/GBM path. Safe to call before
// MeleeFlipInitDisplay(); it only reads the environment.
bool MeleeFlipSdlDisplaySelected();
