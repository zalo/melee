#!/bin/zsh
set -eu
cd "${0:A:h}/.."
unset MELEE_INPUT_SCRIPT MELEE_TEST_SEED MELEE_TRACE_ASSETS
print 'Melee: WASD bewegen | X Angriff/Bestätigen | Z Spezial/Zurück'
print 'C/V springen | Q/E Schild | R greifen | IJKL C-Stick | Return Start/Pause'
print 'Zum Spielen ins Spielfenster klicken. Keine automatische Teststeuerung.'
export ASAN_OPTIONS=detect_leaks=0:color=never
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
exec build/native/melee_mac.app/Contents/MacOS/melee_mac \
  'build/disc/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso'
