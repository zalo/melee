#pragma once
void MeleeFlipInitDisplay();
// Scanout size chosen by MeleeFlipInitDisplay (the connector mode); Aurora renders at it.
extern "C" void MeleeFlipDisplaySize(unsigned* width, unsigned* height);
