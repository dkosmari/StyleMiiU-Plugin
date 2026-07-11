#pragma once
#include <content_redirection/redirection.h>
#include <string>
#include <unordered_set>

#define VERSION "v0.5.2+"

#define KEY_THEME_MANAGER_ENABLED "themeManagerEnabled"
#define KEY_SHUFFLE_THEMES        "suffleThemes"
#define KEY_MASHUP_THEMES         "mashupThemes"
#define KEY_SHOW_NOTIFICATION     "showNotification"
#define KEY_ENABLED_THEMES        "enabledThemes"

#define DEFAULT_THEME_MANAGER_ENABLED true
#define DEFAULT_SHUFFLE_THEMES        false
#define DEFAULT_MASHUP_THEMES         false
#define DEFAULT_SHOW_NOTIFICATION     true

extern bool gThemeManagerEnabled;
extern bool gShuffleThemes;
extern bool gMashupThemes;
extern bool gShowNotification;
extern std::unordered_set<std::string> gEnabledThemes;
extern const std::string theme_directory_path;

extern std::string gLoadedTheme;
extern std::string gLoadedMenTheme;
extern std::string gLoadedMen2Theme;
extern std::string gLoadedCafeBaristaTheme;

extern CRLayerHandle gContentLayerHandle;
