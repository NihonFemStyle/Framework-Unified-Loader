#pragma once

// FULVM.h
// Gates the Oreans Themida = SecureEngine markers. The normal (public) build has
// no Themida dependency, so every marker compiles out to nothing. The "Secure"
// build (FUL_SECURE preprocessor define) includes the vendored ThemidaSDK.h and
// keeps the imported SecureEngineSDK64.lib calls for Themida to VM-protect.

#ifdef FUL_SECURE

#include "ThemidaSDK.h"

#else

#define VM_FISH_WHITE_START
#define VM_FISH_WHITE_END
#define VM_FISH_RED_START
#define VM_FISH_RED_END
#define VM_FISH_BLACK_START
#define VM_FISH_BLACK_END
#define VM_FALCON_TINY_START
#define VM_FALCON_TINY_END
#define VM_TIGER_WHITE_START
#define VM_TIGER_WHITE_END
#define VM_TIGER_RED_START
#define VM_TIGER_RED_END
#define VM_TIGER_BLACK_START
#define VM_TIGER_BLACK_END
#define VM_SHARK_WHITE_START
#define VM_SHARK_WHITE_END
#define VM_SHARK_RED_START
#define VM_SHARK_RED_END
#define VM_SHARK_BLACK_START
#define VM_SHARK_BLACK_END
#define VM_DOLPHIN_WHITE_START
#define VM_DOLPHIN_WHITE_END
#define VM_DOLPHIN_BLACK_START
#define VM_DOLPHIN_BLACK_END
#define VM_DOLPHIN_RED_START
#define VM_DOLPHIN_RED_END
#define VM_EAGLE_WHITE_START
#define VM_EAGLE_WHITE_END
#define VM_EAGLE_RED_START
#define VM_EAGLE_RED_END
#define VM_EAGLE_BLACK_START
#define VM_EAGLE_BLACK_END
#define VM_LION_WHITE_START
#define VM_LION_WHITE_END
#define VM_LION_RED_START
#define VM_LION_RED_END
#define VM_LION_BLACK_START
#define VM_LION_BLACK_END
#define VM_PUMA_WHITE_START
#define VM_PUMA_WHITE_END
#define VM_PUMA_RED_START
#define VM_PUMA_RED_END
#define VM_PUMA_BLACK_START
#define VM_PUMA_BLACK_END
#define MUTATE_START
#define MUTATE_END

#endif