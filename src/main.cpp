#include "globals.h"
#include "utils/logger.h"
#include <dirent.h>
#include <themeSelector.h>
#include <fs/DirList.h>
#include <utils/StringTools.h>

#include <notifications/notifications.h>
#include <content_redirection/redirection.h>

#include <coreinit/title.h>
#include <coreinit/launch.h>
#include <sysapp/title.h>
#include <sysapp/launch.h>

#include <wups.h>
#include <wups/config.h>
#include <wups/config_api.h>
#include <wups/config/WUPSConfigItemStub.h>
#include <wups/storage.h>

#include "wupsext/ConfigItemTheme.h"
#include "wupsext/ConfigItemBooleanVar.h"

#include <vector>
#include <map>
#include <random>
#include <algorithm>
#include <ctime>
#include <chrono>
#include <functional>
#include <ranges>
#include <iterator>

WUPS_PLUGIN_NAME("StyleMiiU");
WUPS_PLUGIN_DESCRIPTION("A way to easily load custom themes");
WUPS_PLUGIN_VERSION(VERSION);
WUPS_PLUGIN_AUTHOR("Juanen100");
WUPS_PLUGIN_LICENSE("GPLv3");

WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("style-mii-u");

using namespace std::literals;

bool need_to_restart = false;
bool is_wiiu_menu = false;

bool isValidThemeDirectory(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
        return false;
    }

    struct dirent* entry;
    struct stat entryInfo;
    bool validTheme = false;

    while ((entry = readdir(dir)) != nullptr) {
        std::string entryPath = path + "/" + entry->d_name;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (stat(entryPath.c_str(), &entryInfo) == 0) {
            if (S_ISDIR(entryInfo.st_mode)) {
                if (isValidThemeDirectory(entryPath)) {
                    validTheme = true;
                    break;
                }
            } else if (S_ISREG(entryInfo.st_mode)) {
                if (strcmp(entry->d_name, "Men.pack") == 0 ||
                    strcmp(entry->d_name, "Men2.pack") == 0 ||
                    strcmp(entry->d_name, "cafe_barista_men.bfsar") == 0) {
                    validTheme = true;
                    break;
                }
            }
        }
    }

    closedir(dir);
    return validTheme;
}

std::unordered_set<std::string> ParseEnabledThemes(const std::string& themes) {
    std::unordered_set<std::string> result;
    if (!gShuffleThemes) {
        if (!themes.empty())
            result = { themes };
    } else {
        auto split_themes = StringTools::stringSplit(themes, "|");
        for (auto& theme : split_themes)
            if (!theme.empty())
                result.insert(std::move(theme));
    }
    return result;
}

static bool IgnoreCaseLess(const std::string& a,
                           const std::string& b)
{
    int r = strcasecmp(a.data(), b.data());
    return r < 0;
}

std::string SerializeEnabledThemes(const std::unordered_set<std::string>& themes) {
    if (!gShuffleThemes) {
        if (themes.empty())
            return "";
        else
            return *themes.begin();
    } else {
        std::vector<std::string> sorted_themes{themes.begin(), themes.end()};
        std::ranges::sort(sorted_themes, IgnoreCaseLess);
        std::string result;
        for (const auto& theme : sorted_themes)
            result += theme + "|";
        return result;
    }
}

void ReloadConfig() {
    using WUPSStorageAPI::GetOrStoreDefault;

    if (auto err = GetOrStoreDefault(KEY_THEME_MANAGER_ENABLED,
                                     gThemeManagerEnabled,
                                     DEFAULT_THEME_MANAGER_ENABLED)) {
        DEBUG_FUNCTION_LINE_ERR("Failed to get or create %s: %s (%d)",
                                KEY_THEME_MANAGER_ENABLED,
                                WUPSStorageAPI_GetStatusStr(err),
                                err);
    }

    if (auto err = GetOrStoreDefault(KEY_SHUFFLE_THEMES,
                                     gShuffleThemes,
                                     DEFAULT_SHUFFLE_THEMES)) {
        DEBUG_FUNCTION_LINE_ERR("Failed to get or create %s: %s (%d)",
                                KEY_SHUFFLE_THEMES,
                                WUPSStorageAPI_GetStatusStr(err),
                                err);
    }

    if (auto err = GetOrStoreDefault(KEY_MASHUP_THEMES,
                                     gMashupThemes,
                                     DEFAULT_MASHUP_THEMES)) {
        DEBUG_FUNCTION_LINE_ERR("Failed to get or create %s: %s (%d)",
                                KEY_MASHUP_THEMES,
                                WUPSStorageAPI_GetStatusStr(err),
                                err);
    }

    if (auto err = GetOrStoreDefault(KEY_SHOW_NOTIFICATION,
                                     gShowNotification,
                                     DEFAULT_SHOW_NOTIFICATION)) {
        DEBUG_FUNCTION_LINE_ERR("Failed to get or create %s: %s (%d)",
                                KEY_MASHUP_THEMES,
                                WUPSStorageAPI_GetStatusStr(err),
                                err);
    }

    gEnabledThemes.clear();
    std::string rawEnabledThemes;
    if (auto err = GetOrStoreDefault(KEY_ENABLED_THEMES,
                                     rawEnabledThemes,
                                     ""s)) {
        DEBUG_FUNCTION_LINE_ERR("Failed to get or create %s: %s (%d)",
                                KEY_ENABLED_THEMES,
                                WUPSStorageAPI_GetStatusStr(err),
                                err);
    } else {
        gEnabledThemes = ParseEnabledThemes(rawEnabledThemes);
    }

    for (auto& theme : gEnabledThemes) {
        DEBUG_FUNCTION_LINE_VERBOSE("Enabled: %s", theme.data());
    }
}

static void theme_manager_enabled_changed_callback(wupsext::ConfigItemBooleanVarContext &item) {
    DEBUG_FUNCTION_LINE_VERBOSE("New value in %s changed: %s",
                                item.identifier.data(),
                                item.variable ? "true" : "false");
    need_to_restart = true;
}

static void shuffle_themes_changed_callback(wupsext::ConfigItemBooleanVarContext &item) {
    DEBUG_FUNCTION_LINE_VERBOSE("New value in %s changed: %s",
                                item.identifier.data(),
                                item.variable ? "true" : "false");
    // When toggling shuffle, it's not clear what the current theme should be, so let's
    // disable them all.
    wupsext::ConfigItemTheme_ResetAll();
    need_to_restart = true;
}

static void mashup_themes_changed_callback(wupsext::ConfigItemBooleanVarContext &item) {
    DEBUG_FUNCTION_LINE_VERBOSE("New value in %s changed: %s",
                                item.identifier.data(),
                                item.variable ? "true" : "false");
}

static void show_notification_changed_callback(wupsext::ConfigItemBooleanVarContext &item) {
    DEBUG_FUNCTION_LINE_VERBOSE("New value in %s changed: %s",
                                item.identifier.data(),
                                item.variable ? "true" : "false");
}

static void theme_changed_callback(wupsext::ConfigItemThemeContext &item) {
    DEBUG_FUNCTION_LINE_VERBOSE("New value in %s changed: %s",
                                item.identifier.data(),
                                item.value ? "true" : "false");
    if (!gShuffleThemes)
        need_to_restart = true;
}

static WUPSConfigAPICallbackStatus ConfigMenuOpenedCallback(WUPSConfigCategoryHandle rootHandle) {
    try {
        WUPSStorageAPI::ForceReloadStorage();
        ReloadConfig();

        WUPSConfigCategory root = WUPSConfigCategory(rootHandle);

        using BoolItem = wupsext::ConfigItemBooleanVar;

        root.add(BoolItem::Create(KEY_THEME_MANAGER_ENABLED,
                                  "Enable StyleMiiU",
                                  gThemeManagerEnabled,
                                  theme_manager_enabled_changed_callback));

        root.add(BoolItem::Create(KEY_SHOW_NOTIFICATION,
                                  "Show theme notification",
                                  gShowNotification,
                                  show_notification_changed_callback));

        root.add(BoolItem::Create(KEY_SHUFFLE_THEMES,
                                  "Shuffle themes",
                                  gShuffleThemes,
                                  shuffle_themes_changed_callback));

        root.add(BoolItem::Create(KEY_MASHUP_THEMES,
                                  "Mashup themes",
                                  gMashupThemes,
                                  mashup_themes_changed_callback));

        // Show last loaded theme
        if (!gLoadedTheme.empty())
            root.add(WUPSConfigItemStub::Create("Loaded theme: "
                                                + gLoadedTheme));

        if (!gLoadedMenTheme.empty()) {
            root.add(WUPSConfigItemStub::Create("Loaded Content: "
                                                + gLoadedMenTheme));
            root.add(WUPSConfigItemStub::Create("Loaded Men: "
                                                + gLoadedMenTheme));
        }

        if (!gLoadedMen2Theme.empty())
            root.add(WUPSConfigItemStub::Create("Loaded Men2: "
                                                + gLoadedMen2Theme));

        if (!gLoadedCafeBaristaTheme.empty())
            root.add(WUPSConfigItemStub::Create("Loaded cafe_barista_men: "
                                                + gLoadedCafeBaristaTheme));

        auto themes = WUPSConfigCategory::Create("Available Themes");

        DirList themeDirList(theme_directory_path, nullptr, DirList::Dirs);
        themeDirList.SortList();

        for(int i = 0; i < themeDirList.GetFilecount(); i++)
        {
            std::string curTheme = themeDirList.GetFilename(i);
            if(curTheme == "." || curTheme == "..")
                continue;

            std::string themeDirPath = std::string(theme_directory_path) + "/" + curTheme;
            if (isValidThemeDirectory(themeDirPath)) {
                bool themeEnabled = gEnabledThemes.contains(curTheme);
                DEBUG_FUNCTION_LINE_WARN("%s: enabled: %s",
                                         curTheme.data(),
                                         themeEnabled ? "true" : "false");
                auto configBool = wupsext::ConfigItemTheme::Create(curTheme,
                                                                   curTheme,
                                                                   themeEnabled,
                                                                   theme_changed_callback);

                themes.add(std::move(configBool));
            }
        }

        root.add(std::move(themes));
    }
    catch (std::exception &e) {
        DEBUG_FUNCTION_LINE_ERR("Exception: %s\n", e.what());
        return WUPSCONFIG_API_CALLBACK_RESULT_ERROR;
    }
    return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
}

static void ConfigMenuClosedCallback() {
    // Update gEnabledThemes based on the enabled items.
    gEnabledThemes.clear();
    wupsext::ConfigItemTheme_ForEach(
        [](wupsext::ConfigItemThemeContext &item)
        {
            if (item.value) {
                DEBUG_FUNCTION_LINE_VERBOSE("Found enabled: %s", item.identifier.data());
                gEnabledThemes.insert(item.identifier);
            }
        }
    );
    wupsext::ConfigItemTheme_Clear();

    using WUPSStorageAPI::Store;

    if (!gShuffleThemes && gEnabledThemes.size() > 1)
        DEBUG_FUNCTION_LINE_WARN("Inconsistent state: gEnableThemes.size() > 1 but gShuffleThemes is false!");

    if (auto err = Store(KEY_ENABLED_THEMES, SerializeEnabledThemes(gEnabledThemes))) {
            DEBUG_FUNCTION_LINE_WARN("Failed to store %s: %s (%d)",
                                     KEY_ENABLED_THEMES,
                                     WUPSStorageAPI_GetStatusStr(err),
                                     err);
    }

    if (auto err = Store(KEY_THEME_MANAGER_ENABLED, gThemeManagerEnabled)) {
        DEBUG_FUNCTION_LINE_ERR("Failed to store %s: %s (%d)",
                                KEY_THEME_MANAGER_ENABLED,
                                WUPSStorageAPI_GetStatusStr(err),
                                err);
    }

    if (auto err = Store(KEY_SHUFFLE_THEMES, gShuffleThemes)) {
        DEBUG_FUNCTION_LINE_ERR("Failed to store %s: %s (%d)",
                                KEY_SHUFFLE_THEMES,
                                WUPSStorageAPI_GetStatusStr(err),
                                err);
    }

    if (auto err = Store(KEY_MASHUP_THEMES, gMashupThemes)) {
        DEBUG_FUNCTION_LINE_ERR("Failed to store %s: %s (%d)",
                                KEY_MASHUP_THEMES,
                                WUPSStorageAPI_GetStatusStr(err),
                                err);
    }

    if (auto err = Store(KEY_SHOW_NOTIFICATION, gShowNotification)) {
        DEBUG_FUNCTION_LINE_ERR("Failed to store %s: %s (%d)",
                                KEY_SHOW_NOTIFICATION,
                                WUPSStorageAPI_GetStatusStr(err),
                                err);
    }

    if (auto err = WUPSStorageAPI::SaveStorage()) {
        DEBUG_FUNCTION_LINE_ERR("Failed to save storage: %s (%d)",
                                WUPSStorageAPI_GetStatusStr(err),
                                err);
    }


    if(need_to_restart && is_wiiu_menu)
    {
        SYSLaunchMenu();
        need_to_restart = false;
    }
}

INITIALIZE_PLUGIN() {
    if (auto err = ContentRedirection_InitLibrary()) {
        DEBUG_FUNCTION_LINE_ERR("Failed to init ContentRedirection. Error %s %d",
                                ContentRedirection_GetStatusStr(err),
                                err);
        OSFatal("Failed to init ContentRedirection.");
    }

    if (NotificationModule_InitLibrary()) {
        DEBUG_FUNCTION_LINE("NotificationModule_InitLibrary failed");
        gShowNotification = false;
    }

    WUPSConfigAPIOptionsV1 configOptions = {.name = "StyleMiiU"};
    if (auto err = WUPSConfigAPI_Init(configOptions,
                                      ConfigMenuOpenedCallback,
                                      ConfigMenuClosedCallback)) {
        DEBUG_FUNCTION_LINE_ERR("Failed to init config api: %s (%d)",
                                WUPSConfigAPI_GetStatusStr(err),
                                err);
    }

    ReloadConfig();

    if (auto err = WUPSStorageAPI::SaveStorage()) {
        DEBUG_FUNCTION_LINE_ERR("Failed to save storage: %s (%d)",
                                WUPSStorageAPI_GetStatusStr(err),
                                err);
    }

    gContentLayerHandle = 0;
}

DEINITIALIZE_PLUGIN() {
    NotificationModule_DeInitLibrary();
}

static std::size_t get_random_index(std::size_t size) {
    static std::optional<std::minstd_rand> engine;
    if (!engine) {
        auto t = static_cast<std::uint64_t>(OSGetTime());
        std::seed_seq seeder{static_cast<std::uint32_t>(t),
                             static_cast<std::uint32_t>(t >> 32)};
        engine.emplace(seeder);
    }
    std::uniform_int_distribution<std::size_t> dist{0, size - 1};
    return dist(*engine);
}

static std::string get_random_item(const std::unordered_set<std::string>& bundle) {
    if (bundle.empty())
        return "";
    auto randomIndex = get_random_index(bundle.size());
    auto it = bundle.begin();
    std::advance(it, randomIndex);
    return *it;
}

void SearchThemeFiles(const std::string& themePath, std::string& outMenPack, std::string& outMen2Pack, std::string& outCafeBarista) {
    DIR* dir = opendir(themePath.c_str());
    if (dir == nullptr) {
        return;
    }

    struct dirent* entry;
    struct stat entryInfo;

    while ((entry = readdir(dir)) != nullptr) {
        std::string entryPath = themePath + "/" + entry->d_name;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (stat(entryPath.c_str(), &entryInfo) == 0) {
            if (S_ISDIR(entryInfo.st_mode)) {
                std::string subMenPack, subMen2Pack, subCafeBarista;
                SearchThemeFiles(entryPath, subMenPack, subMen2Pack, subCafeBarista);

                if (!subMenPack.empty() && outMenPack.empty()) outMenPack = subMenPack;
                if (!subMen2Pack.empty() && outMen2Pack.empty()) outMen2Pack = subMen2Pack;
                if (!subCafeBarista.empty() && outCafeBarista.empty()) outCafeBarista = subCafeBarista;
            }
            else if (S_ISREG(entryInfo.st_mode)) {
                if (strcmp(entry->d_name, "Men.pack") == 0 && outMenPack.empty()) {
                    outMenPack = entryPath;
                } else if (strcmp(entry->d_name, "Men2.pack") == 0 && outMen2Pack.empty()) {
                    outMen2Pack = entryPath;
                } else if (strcmp(entry->d_name, "cafe_barista_men.bfsar") == 0 && outCafeBarista.empty()) {
                    outCafeBarista = entryPath;
                }
            }
        }
    }

    closedir(dir);
}

void HandleThemes()
{
    bool success = false;

    std::string menPackPath;
    std::string men2PackPath;
    std::string cafeBaristaPath;
    std::string contentPath;

    gLoadedTheme.clear();
    gLoadedMenTheme.clear();
    gLoadedMen2Theme.clear();
    gLoadedCafeBaristaTheme.clear();

    if (gEnabledThemes.empty()) {
        DEBUG_FUNCTION_LINE_WARN("no themes enabled");
        return;
    }

    if (gMashupThemes && gShuffleThemes)
    {
        std::string tempMenPack, tempMen2Pack, tempCafeBarista;

        int attempts = 0;
        const int maxAttempts = gEnabledThemes.size() * 2;

        while (menPackPath.empty() && attempts < maxAttempts) {
            std::string randomTheme = get_random_item(gEnabledThemes);
            std::string themePath = theme_directory_path + randomTheme;

            tempMenPack.clear();
            tempMen2Pack.clear();
            tempCafeBarista.clear();

            SearchThemeFiles(themePath, tempMenPack, tempMen2Pack, tempCafeBarista);

            if (!tempMenPack.empty()) {
                menPackPath = tempMenPack;
                gLoadedMenTheme = randomTheme;
                break;
            }
            attempts++;
        }

        attempts = 0;
        while (men2PackPath.empty() && attempts < maxAttempts) {
            std::string randomTheme = get_random_item(gEnabledThemes);
            std::string themePath = theme_directory_path + randomTheme;

            tempMenPack.clear();
            tempMen2Pack.clear();
            tempCafeBarista.clear();

            SearchThemeFiles(themePath, tempMenPack, tempMen2Pack, tempCafeBarista);

            if (!tempMen2Pack.empty()) {
                men2PackPath = tempMen2Pack;
                gLoadedMen2Theme = randomTheme;
                break;
            }
            attempts++;
        }

        attempts = 0;
        while (cafeBaristaPath.empty() && attempts < maxAttempts) {
            std::string randomTheme = get_random_item(gEnabledThemes);
            std::string themePath = theme_directory_path + randomTheme;

            tempMenPack.clear();
            tempMen2Pack.clear();
            tempCafeBarista.clear();

            SearchThemeFiles(themePath, tempMenPack, tempMen2Pack, tempCafeBarista);

            if (!tempCafeBarista.empty()) {
                cafeBaristaPath = tempCafeBarista;
                gLoadedCafeBaristaTheme = randomTheme;
                break;
            }
            attempts++;
        }

        struct stat st {};
        contentPath = theme_directory_path + gLoadedMenTheme + "/content";
        if (stat(contentPath.c_str(), &st) != 0){
            contentPath = theme_directory_path + gLoadedMenTheme + "/Content";
            if (stat(contentPath.c_str(), &st) != 0){
                contentPath = "";
            }
        }
    }
    else {
        if (gShuffleThemes)
            gLoadedTheme = get_random_item(gEnabledThemes);
        else
            gLoadedTheme = *gEnabledThemes.begin();

        const std::string currentThemePath = theme_directory_path + gLoadedTheme;

        SearchThemeFiles(currentThemePath, menPackPath, men2PackPath, cafeBaristaPath);

        struct stat st {};
        contentPath = currentThemePath + "/content";
        if (stat(contentPath.c_str(), &st) != 0){
            contentPath = currentThemePath + "/Content";
            if (stat(contentPath.c_str(), &st) != 0){
                contentPath = "";
            }
        }
    }

    if (!menPackPath.empty() || !men2PackPath.empty() || !cafeBaristaPath.empty() || !contentPath.empty()) {
        success = ReplaceContent(menPackPath, men2PackPath, cafeBaristaPath, contentPath);
    }

    if (success && gShowNotification){
        auto res = NotificationModule_SetDefaultValue(
            NOTIFICATION_MODULE_NOTIFICATION_TYPE_INFO,
            NOTIFICATION_MODULE_DEFAULT_OPTION_DURATION_BEFORE_FADE_OUT,
            12.0f);
        if (res)
            return;

        std::string text = "Theme: ";
        if (gMashupThemes) {
            if (!contentPath.empty()){
                text = "Content: ";
                text.append(gLoadedMenTheme);
                NotificationModule_AddInfoNotification(text.c_str());
            }

            if (!cafeBaristaPath.empty()){
                text = "cafe_barista_men.bfsar: ";
                text.append(gLoadedCafeBaristaTheme);
                NotificationModule_AddInfoNotification(text.c_str());
            }

            if (!men2PackPath.empty()){
                text = "Men2.pack: ";
                text.append(gLoadedMen2Theme);
                NotificationModule_AddInfoNotification(text.c_str());
            }

            if (!menPackPath.empty()){
                text = "Men.pack: ";
                text.append(gLoadedMenTheme);
                NotificationModule_AddInfoNotification(text.c_str());
            }
            return;
        } else {
            text.append(gLoadedTheme);
            NotificationModule_AddInfoNotification(text.c_str());
        }
    }
}

ON_APPLICATION_START() {
    initLogging();

    uint64_t current_title_id = OSGetTitleID();
    uint64_t wiiu_menu_tid = _SYSGetSystemApplicationTitleId(SYSTEM_APP_ID_WII_U_MENU);

    is_wiiu_menu = (current_title_id == wiiu_menu_tid);

    if (!is_wiiu_menu) return;

    WUPSStorageAPI::ForceReloadStorage();

    ReloadConfig();

    if (!gThemeManagerEnabled) return;

    HandleThemes();
}

ON_APPLICATION_ENDS() {
    if (gContentLayerHandle != 0) {
        ContentRedirection_RemoveFSLayer(gContentLayerHandle);
        gContentLayerHandle = 0;
    }
    deinitLogging();
}
