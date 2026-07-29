// Seeed_GFX picks this up via User_Setup_Select.h (__has_include("driver.h")).
// PIO also passes these via build_flags; keep ifndef so both paths coexist.
#if defined(SEEED_EE05)
#ifndef BOARD_SCREEN_COMBO
#define BOARD_SCREEN_COMBO 508 // 2.13" mono SSD1680 (122×250 usable)
#endif
#ifndef USE_XIAO_EPAPER_DISPLAY_BOARD_EE05
#define USE_XIAO_EPAPER_DISPLAY_BOARD_EE05
#endif
#endif
