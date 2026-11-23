#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <flat_map>
#include <flat_set>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <coreinit/launch.h>
#include <coreinit/memory.h>
#include <coreinit/thread.h>
#include <coreinit/title.h>
#include <sysapp/launch.h>
#include <sysapp/title.h>

#include <content_redirection/redirection.h>

#include <notifications/notifications.h>

#include <glaze/beve.hpp>
#include <glaze/json.hpp>
#include <glaze/exceptions/core_exceptions.hpp>
#include <glaze/exceptions/json_exceptions.hpp>

#include <wups.h>
#include <wups/config.h>
#include <wups/config_api.h>
#include <wups/config/WUPSConfigItemStub.h>
#include <wups/storage.h>

#include "wupsext/ConfigToggleItem.h"
#include "wupsext/ConfigBooleanVarItem.h"

#include "globals.h"
#include "themeSelector.h"
#include "utils/File.h"
#include "utils/IgnoreCase.h"
#include "utils/logger.h"
#include "utils/Timer.h"

#define ENABLE_CACHE

// NOTE: Runtime for JSON cache is bigger than BEVE cache..
#define CACHE_JSON

// NOTE: Runtime for flat_map/flat_set is bigger than map/set.
// #define USE_FLAT

WUPS_PLUGIN_NAME("StyleMiiU");
WUPS_PLUGIN_DESCRIPTION("A way to easily load custom themes");
WUPS_PLUGIN_VERSION(VERSION);
WUPS_PLUGIN_AUTHOR("Juanen100");
WUPS_PLUGIN_LICENSE("GPLv3");

WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("style-mii-u");

using namespace std::literals;

namespace {

    enum class AssetType {
        BGM,
        Font,
    };

    using PathVec   = std::vector<std::filesystem::path>;
    using StringVec = std::vector<std::string>;

#ifdef USE_FLAT
    using PathSet   = std::flat_set<std::filesystem::path, utils::IgnoreCase::Less>;
    using StringSet = std::flat_set<std::string, utils::IgnoreCase::Less>;
#else
    using PathSet   = std::set<std::filesystem::path, utils::IgnoreCase::Less>;
    using StringSet = std::set<std::string, utils::IgnoreCase::Less>;
#endif

    // NOTE: Define these types here so we can break out of the anonymous namespace to
    // specialize the Glaze serialization.

    struct PrefGroup {
        std::optional<StringSet> ids;
        std::optional<StringSet> names;
        std::optional<StringSet> tags;

        void
        merge(const PrefGroup& other);
    };

    struct AssetPreferences {
        std::optional<PrefGroup> prefer;
        std::optional<PrefGroup> avoid;

        void
        merge(const AssetPreferences& other);
    };

    struct Preferences {
        std::optional<AssetPreferences> bgm;
        std::optional<AssetPreferences> font;
    };

    struct Metadata {
        std::optional<std::string> themeID;
        std::string themeName;
        std::optional<std::string> themeAuthor;
        std::optional<std::string> themeVersion;
        std::optional<StringSet> tags;

        std::optional<Preferences> preferences;
    };

    // NOTE: Avoid having the same name for type and variable, it causes compilation errors.
    struct MetadataJson {
        Metadata metadata;
    };

    struct BasicInfo {
        std::filesystem::path dir = {}; // Relative to root path, empty for standalone assets.
        std::optional<Metadata> metadata = {};

        std::string
        getName()
            const;

        std::filesystem::path
        resolve(const std::filesystem::path& rootDir,
                const std::filesystem::path& relativeFile)
            const;
    };

    struct AssetInfo : BasicInfo {
        PathVec files = {};     // relative to parentDir/filename

        std::string
        getName()
            const;
    };

    struct ThemeInfo : BasicInfo {
        std::filesystem::path menPack             = {}; // Relative to root/dir.
        std::filesystem::path men2Pack            = {}; // Relative to root/dir.
        std::filesystem::path cafeBaristaMenBfsar = {}; // Relative to root/dir.

        PathVec bundledBFSTMFiles = {}; // Relative to root/dir.
        PathVec bundledTTFFiles   = {}; // Relative to root/dir.

        std::optional<Preferences> preferences;

        std::optional<AssetPreferences>
        getAssetPreferences(AssetType type)
            const;
    };

} // namespace

// Start of Glaze customization

// NOTE: The built-in pascal_case transformer will make the first letter uppercase, because
// the JSON actually names the variable "Metadata".
template <>
struct glz::meta<MetadataJson> : glz::pascal_case {};

// NOTE: Glaze doesn't like inheritance.
template <>
struct glz::meta<AssetInfo> {
    using T = AssetInfo;

    static constexpr
    auto value = object(
        &T::dir,                // inherited from BasicInfo
        &T::metadata,           // inherited from BasicInfo
        &T::files
    );
};

// NOTE: Glaze doesn't like inheritance.
template <>
struct glz::meta<ThemeInfo> {
    using T = ThemeInfo;

    static constexpr
    auto value = object(
        &T::dir,                // inherited from BasicInfo
        &T::metadata,           // inherited from BasicInfo
        &T::menPack,
        &T::men2Pack,
        &T::cafeBaristaMenBfsar,
        &T::bundledBFSTMFiles,
        &T::bundledTTFFiles,
        &T::preferences
    );
};

// End of Glaze customization

namespace {

    /*-------*/
    /* Types */
    /*-------*/

#ifdef USE_FLAT
    using AssetMap = std::flat_map<std::string, AssetInfo, utils::IgnoreCase::Less>;
    using ThemeMap = std::flat_map<std::string, ThemeInfo, utils::IgnoreCase::Less>;
#else
    using AssetMap = std::map<std::string, AssetInfo, utils::IgnoreCase::Less>;
    using ThemeMap = std::map<std::string, ThemeInfo, utils::IgnoreCase::Less>;
#endif

    using FontBlob = std::vector<char>;

    struct JsonReadOptions : glz::opts {
        bool error_on_unknown_keys = false;
        bool error_on_missing_keys = false;
        bool validate_utf8 = false;
    };

    struct ScanResult {
        ThemeMap themes;
        AssetMap bgms;
        AssetMap fonts;
    };

#ifdef ENABLE_CACHE

    struct Cache {
        static constexpr int currentVersion = 1;

        int version = currentVersion;
        ScanResult result;

        void
        checkVersion()
            const;
    };

    struct CacheOptions : glz::opts {

#ifdef CACHE_JSON
        std::uint32_t format = glz::JSON;
#else
        std::uint32_t format = glz::BEVE;
#endif
        bool error_on_unknown_keys = true;
        bool prettify = true;
        bool error_on_missing_keys = true;
        bool error_on_missing_array_elements = true;
        std::uint8_t indentation_width = 4;
    };

#endif // ENABLE_CACHE

    struct Config {

        bool fontsEnabled{};
        bool globalFont{};
        bool mashupThemes{};
        bool overrideBgm{};
        bool showNotification{};
        bool shuffleBgms{};
        bool shuffleFonts{};
        bool shuffleThemes{};
        bool themeManagerEnabled{};

        StringSet enabledThemes{};
        StringSet enabledBgms{};
        StringSet enabledFonts{};

    };

    struct Loaded {
        std::string theme;
        std::string men;
        std::string men2;
        std::string cafeBarista;
        std::string bgm;
        std::string font;
        std::string content;
    };

    /*-----------*/
    /* Constants */
    /*-----------*/

    constexpr auto KEY_ENABLED_BGMS          = "enabledBgms";
    constexpr auto KEY_ENABLED_FONTS         = "enabledFonts";
    constexpr auto KEY_ENABLED_THEMES        = "enabledThemes";
    constexpr auto KEY_FONTS_ENABLED         = "fontsEnabled";
    constexpr auto KEY_GLOBAL_FONT           = "globalFont";
    constexpr auto KEY_MASHUP_THEMES         = "mashupThemes";
    constexpr auto KEY_OVERRIDE_BGM          = "overrideBgm";
    constexpr auto KEY_SHOW_NOTIFICATION     = "showNotification";
    constexpr auto KEY_SHUFFLE_BGMS          = "shuffleBgms";
    constexpr auto KEY_SHUFFLE_FONTS         = "shuffleFonts";
    constexpr auto KEY_SHUFFLE_THEMES        = "shuffleThemes";
    constexpr auto KEY_SHUFFLE_THEMES_OLD    = "suffleThemes";
    constexpr auto KEY_THEME_MANAGER_ENABLED = "themeManagerEnabled";

    constexpr bool DEFAULT_FONTS_ENABLED         = true;
    constexpr bool DEFAULT_GLOBAL_FONT           = false;
    constexpr bool DEFAULT_MASHUP_THEMES         = false;
    constexpr bool DEFAULT_OVERRIDE_BGM          = false;
    constexpr bool DEFAULT_SHOW_NOTIFICATION     = true;
    constexpr bool DEFAULT_SHUFFLE_BGMS          = false;
    constexpr bool DEFAULT_SHUFFLE_FONTS         = false;
    constexpr bool DEFAULT_SHUFFLE_THEMES        = false;
    constexpr bool DEFAULT_THEME_MANAGER_ENABLED = true;

    const std::filesystem::path wiiu_root   = "fs:/vol/external01/wiiu";
    const std::filesystem::path themes_root = wiiu_root / "themes";
    const std::filesystem::path bgms_root   = wiiu_root / "bgms";
    const std::filesystem::path fonts_root  = wiiu_root / "fonts";

#ifdef ENABLE_CACHE
#ifdef CACHE_JSON
    const std::filesystem::path cache_path = themes_root / "stylemiiu-cache.json";
#else // !CACHE_JSON
    const std::filesystem::path cache_path = themes_root / "stylemiiu-cache.beve";
#endif
#endif // !ENABLE_CACHE

    const std::filesystem::path bgm_bfsar_path     = bgms_root / "BGM.bfsar";
    const std::filesystem::path silence_bfstm_path = bgms_root / "silence.bfstm";

    const std::string specialSilence  = "silence"s;
    const std::string specialCafeStd = "CafeStd"s;

    /*-----------*/
    /* Variables */
    /*-----------*/

    Config gCfg{
        .fontsEnabled        = DEFAULT_FONTS_ENABLED,
        .globalFont          = DEFAULT_GLOBAL_FONT,
        .mashupThemes        = DEFAULT_MASHUP_THEMES,
        .overrideBgm         = DEFAULT_OVERRIDE_BGM,
        .showNotification    = DEFAULT_SHOW_NOTIFICATION,
        .shuffleBgms         = DEFAULT_SHUFFLE_BGMS,
        .shuffleFonts        = DEFAULT_SHUFFLE_FONTS,
        .shuffleThemes       = DEFAULT_SHUFFLE_THEMES,
        .themeManagerEnabled = DEFAULT_THEME_MANAGER_ENABLED,
    };

    std::optional<Config> gOldCfg;

    Loaded gLoaded;

    bool gNeedToRestart = false;
    std::minstd_rand gPRNGEngine;
    std::optional<wupsext::ConfigToggleItemGroup> gThemesGroup;
    std::optional<wupsext::ConfigToggleItemGroup> gBGMsGroup;
    std::optional<wupsext::ConfigToggleItemGroup> gFontsGroup;

    std::mutex gFontStdBlobMutex;
    std::optional<FontBlob> gFontStdBlob;

    std::mutex gScanMutex;
    std::optional<ScanResult> gScanResult;

#ifdef ENABLE_CACHE
    std::jthread gCacheWriteThread;
    bool gDeleteCacheOnExit;
#endif

    /*-----------------------*/
    /* Function declarations */
    /*-----------------------*/

    bool
    CalledFromWiiUMenu()
        noexcept;

    bool
    CalledFromWUPSMenu()
        noexcept;

    bool
    CalledFromWiiUMenuSWKBD()
        noexcept;

    void
    ClearScanResult();

    bool
    ConfigChangeNeedsRestart(const Config& a,
                             const Config& b);

    std::string
    GetBundledFileName(const ThemeInfo& theme,
                       const std::filesystem::path& file);

    ScanResult&
    GetScanResult();

    bool
    HasAvoidAll(const std::optional<AssetPreferences>& assetPrefs);

    bool
    HasTTFMagic(utils::File& f)
        noexcept;

    void
    InitializePRNG();

    bool
    IsBFSTM(const std::filesystem::path& path);

    bool
    IsTTF(const std::filesystem::path& path)
        noexcept;

    bool
    IsUsingBGM(const std::filesystem::path& bfsarPath)
        noexcept;

    template <std::ranges::forward_range R>
    std::string
    JoinStrings(R&& input,
                bool shuffle);

    FontBlob
    LoadFont(const std::filesystem::path& path)
        noexcept;

    template <typename T>
    bool
    LoadVar(const char* key,
            T& var);

    template <typename T>
    bool
    LoadVar(const char* key,
            T& var,
            const T default_value);

    bool
    MatchID(const AssetInfo& asset,
            const PrefGroup& grp);

    bool
    MatchName(const std::string& name,
              const PrefGroup& grp);

    bool
    MatchTags(const AssetInfo& asset,
              const PrefGroup& grp)
        noexcept;

    void
    OnShuffleBGMsChanged(wupsext::ConfigBooleanVarItemImpl &item);

    void
    OnShuffleFontsChanged(wupsext::ConfigBooleanVarItemImpl &item);

    void
    OnShuffleThemesChanged(wupsext::ConfigBooleanVarItemImpl &item);

    void
    PickAndApplyTheme();

    std::filesystem::path
    PickAnyEnabledAsset(AssetType type);

    std::filesystem::path
    PickAnyEnabledBGM();

    std::filesystem::path
    PickAnyEnabledFont();

    std::filesystem::path
    PickFont();

    std::filesystem::path
    PickPreferredAsset(const ThemeInfo& theme,
                       AssetType type);

    std::filesystem::path
    PickPreferredBGM(const ThemeInfo& theme);

    std::filesystem::path
    PickPreferredFont(const ThemeInfo& theme);

    template <std::ranges::forward_range R>
    std::ranges::range_value_t<R>
    PickRandomItem(R&& input);

    AssetInfo
    ReadBGMInfo(const std::filesystem::directory_entry& entry,
                std::string& workBuffer);

#ifdef ENABLE_CACHE

    bool
    ReadCache(ScanResult& result,
              std::string& workBuffer)
        noexcept;

#endif

    AssetInfo
    ReadFontInfo(const std::filesystem::directory_entry& entry,
                 std::string& workBuffer);

    std::optional<Metadata>
    ReadMetadata(const std::filesystem::path& metaPath,
                 std::string& workBuffer);

    ThemeInfo
    ReadThemeInfo(const std::filesystem::directory_entry& entry,
                  std::string& workBuffer);

    void
    ReloadConfig();

    std::filesystem::path
    RemovePrefix(const std::filesystem::path& input,
                 const std::filesystem::path& prefix);

    ScanResult
    ScanFiles()
        noexcept;

    void
    SetFallbacksIfNeeded(ReplaceParams& args);

    StringSet
    SplitString(const std::string& input);

    template <typename T>
    void
    StoreVar(const char* key,
             const T& var);

    std::string
    to_string(AssetType type);

#ifdef ENABLE_CACHE

    void
    WriteCache(std::stop_token stoken,
               ScanResult result)
        noexcept;

#endif

    /*----------------------*/
    /* Function definitions */
    /*----------------------*/

    void
    PrefGroup::merge(const PrefGroup& other)
    {
        if (other.ids)
            ids = other.ids;
        if (other.names)
            names = other.names;
        if (other.tags)
            tags = other.tags;
    }

    void
    AssetPreferences::merge(const AssetPreferences& other)
    {
        if (other.prefer) {
            if (prefer)
                prefer->merge(*other.prefer);
            else
                prefer = other.prefer;
        }
        if (other.avoid) {
            if (avoid)
                avoid->merge(*other.avoid);
            else
                avoid = other.avoid;
        }
    }

    std::string
    BasicInfo::getName()
        const
    {
        if (!dir.empty())
            return dir.filename().native();
        return {};
    }


    std::string
    AssetInfo::getName()
        const
    {
        auto name = BasicInfo::getName();
        if (!name.empty())
            return name;

        if (files.empty())
            return {};

        return files.front().stem().native();
    }

    std::optional<AssetPreferences>
    ThemeInfo::getAssetPreferences(AssetType type)
        const
    {
        std::optional<AssetPreferences> result;

        switch (type) {
            using enum AssetType;

            case BGM:
                if (metadata && metadata->preferences)
                    result = metadata->preferences->bgm;

                if (preferences && preferences->bgm) {
                    if (!result)
                        result.emplace();
                    result->merge(*preferences->bgm);
                }
                break;

            case Font:
                if (metadata && metadata->preferences)
                    result = metadata->preferences->font;

                if (preferences && preferences->font) {
                    if (!result)
                        result.emplace();
                    result->merge(*preferences->font);
                }
                break;
        }

        return result;
    }

    std::filesystem::path
    BasicInfo::resolve(const std::filesystem::path& rootDir,
                       const std::filesystem::path& relativeFile)
        const
    {
        if (relativeFile.empty())
            return {};
        return rootDir / dir / relativeFile;
    }

#ifdef ENABLE_CACHE

    void
    Cache::checkVersion()
        const
    {
        if (version != currentVersion)
            throw std::runtime_error{
                "invalid cache version: should be "s
                + std::to_string(currentVersion)
                + " but got "
                + std::to_string(version)
            };
    }

#endif

    bool
    CalledFromWiiUMenu()
        noexcept
    {
        // NOTE: _SYSGetSystemApplicationTitleId() crashes from inside applets, so let's just
        // hardcode the list of known Wii U Menu title IDs.
        switch (OSGetTitleID()) {
            case 0x00050010'10040000: // JPN
            case 0x00050010'10040100: // USA
            case 0x00050010'10040200: // EUR
                return true;
            default:
                return false;
        }
    }

    bool
    CalledFromWUPSMenu()
        noexcept
    {
        WUPSConfigAPIMenuStatus menu_status = WUPSCONFIG_API_MENU_STATUS_CLOSED;
        WUPSConfigAPI_Menu_GetStatus(&menu_status);
        return menu_status == WUPSCONFIG_API_MENU_STATUS_OPENED;
    }

    bool
    CalledFromWiiUMenuSWKBD()
        noexcept
    {
        OSThread* th_id = OSGetCurrentThread();
        const char* th_name = OSGetThreadName(th_id);
        if (!th_name)
            return false;
        return "MenSwkbdCalculator_Create"sv == th_name;
    }

    void
    ClearScanResult()
    {
        std::lock_guard guard{gScanMutex};
        gScanResult.reset();
    }

    bool
    ConfigChangeNeedsRestart(const Config& a,
                             const Config& b)
    {
        if (a.fontsEnabled != b.fontsEnabled) {
            LOG_INFO("Changed fontsEnabled");
            return true;
        }

        if (a.globalFont != b.globalFont) {
            LOG_INFO("Changed globalFont");
            return true;
        }

        if (a.mashupThemes != b.mashupThemes) {
            LOG_INFO("Changed mashupThemes");
            return true;
        }

        if (a.overrideBgm != b.overrideBgm) {
            LOG_INFO("Changed overrideBgm");
            return true;
        }

        // NOTE: ignore showNotification

        if (a.shuffleBgms != b.shuffleBgms) {
            LOG_INFO("Changed shuffleBgms");
            return true;
        }

        if (a.shuffleFonts != b.shuffleFonts) {
            LOG_INFO("Changed shuffleFonts");
            return true;
        }

        if (a.shuffleThemes != b.shuffleThemes) {
            LOG_INFO("Changed shuffleThemes");
            return true;
        }

        if (a.themeManagerEnabled != b.themeManagerEnabled) {
            LOG_INFO("Changed themeManagerEnabled");
            return true;
        }

        if (a.enabledThemes != b.enabledThemes) {
            LOG_INFO("Changed enabledThemes");
            return true;
        }

        if (a.enabledBgms != b.enabledBgms) {
            LOG_INFO("Changed enabledBgms");
            return true;
        }

        if (a.enabledFonts != b.enabledFonts) {
            LOG_INFO("Changed enabledFonts");
            return true;
        }

        return false;
    }

    std::string
    GetBundledFileName(const ThemeInfo& theme,
                       const std::filesystem::path& file)
    {
        // name = ThemeDir/filename (no ext)
        return (theme.dir / file.stem()).native();
    }

    ScanResult&
    GetScanResult()
    {
        std::lock_guard guard{gScanMutex};
        if (!gScanResult)
            gScanResult = ScanFiles();
        return *gScanResult;
    }

    bool
    HasAvoidAll(const std::optional<AssetPreferences>& assetPrefs)
    {
        if (assetPrefs && assetPrefs->avoid) {
            if (assetPrefs->avoid->ids
                && assetPrefs->avoid->ids->contains("*"sv))
                return true;
            if (assetPrefs->avoid->names
                && assetPrefs->avoid->names->contains("*"sv))
                return true;
            if (assetPrefs->avoid->tags
                && assetPrefs->avoid->tags->contains("*"sv))
                return true;
        }
        return false;
    }

    bool
    HasTTFMagic(utils::File& f)
        noexcept
    {
        try {
            constexpr std::array<char, 4> ttf_magic = {0x00, 0x01, 0x00, 0x00};
            std::array<char, 4> file_magic;
            f.rewind();
            auto res = f.read(file_magic);
            f.rewind();
            if (res != 4)
                return false;
            return ttf_magic == file_magic;
        }
        catch (std::exception&){
            return false;
        }
    }

    void
    InitializePRNG()
    {
        auto t = static_cast<std::uint64_t>(OSGetTime());
        std::seed_seq seeder{
            static_cast<std::uint32_t>(t),
            static_cast<std::uint32_t>(t >> 32)
        };
        gPRNGEngine.seed(seeder);
    }

    bool
    IsBFSTM(const std::filesystem::path& path)
    {
        return utils::IgnoreCase::Equal(path.extension(), ".bfstm"sv);
    }

    bool
    IsTTF(const std::filesystem::path& path)
        noexcept
    {
        return utils::IgnoreCase::Equal(path.extension(), ".ttf"sv);
    }

    bool
    IsUsingBGM(const std::filesystem::path& bfsarPath)
        noexcept
    {
        try {
            if (bfsarPath.empty())
                return false;

            std::string buf(256 * 1024, '\0');
            {
                utils::File f{bfsarPath, "rb"};
                auto r = f.read(buf);
                buf.resize(r);
            }
            return buf.contains("\0BGM.bfstm\0"sv);
        }
        catch (std::exception& e) {
            LOG_ERROR("Error checking \"%s\": %s", bfsarPath.c_str(), e.what());
            return false;
        }
    }

    template <std::ranges::forward_range R>
    std::string
    JoinStrings(R&& input,
                bool shuffle)
    {
        if (!shuffle) {
            if (input.empty())
                return "";
            else
                return *input.begin();
        } else {
            StringSet sorted{input.begin(), input.end()};
            std::string result;
            for (const auto& item : sorted)
                result += item + "|";
            return result;
        }
    }

    FontBlob
    LoadFont(const std::filesystem::path& fontPath)
        noexcept
    {
        using std::runtime_error;

        try {
            if (utils::IgnoreCase::Equal(fontPath, specialCafeStd)) {
                LOG_INFO("Skipping font load for CafeStd.");
                return {}; // causes CafeStd to be loaded
            }

            if (fontPath.empty())
                throw runtime_error{"Font path is empty!"};
            if (!exists(fontPath))
                throw runtime_error{"Font file does not exist!"};
            if (!is_regular_file(fontPath))
                throw runtime_error{"Font file is not a regular file!"};

            auto size = file_size(fontPath);
            if (size < 8)
                throw runtime_error{"Font file is too small!"};

            FontBlob blob(size);

            utils::File f{fontPath, "rb"};
            if (!HasTTFMagic(f))
                throw runtime_error{"TTF magic mismatch!"};
            auto res = f.read(blob);
            if (std::cmp_not_equal(res, size))
                throw runtime_error{"Could not load entire font file!"};

            LOG_INFO("Loaded font: %s",
                     fontPath.c_str());
            auto fontRel = RemovePrefix(fontPath, fonts_root);
            fontRel.replace_extension();
            gLoaded.font = fontRel.native();
            if (gCfg.showNotification) {
                std::string text = "Font: " + gLoaded.font;
                NotificationModule_AddInfoNotification(text.c_str());
            }

            return blob;
        }
        catch (std::exception& e) {
            LOG_ERROR("error loading font \"%s\": %s", fontPath.c_str(), e.what());
        }
        catch (...) {
            LOG_ERROR("unknown error loading font \"%s\"", fontPath.c_str());
        }

        return {};
    }

    template <typename T>
    bool
    LoadVar(const char* key,
            T& var)
    {
        if (auto err = WUPSStorageAPI::Get(key, var)) {
            LOG_ERROR("Failed to get %s: %s (%d)",
                      key,
                      WUPSStorageAPI_GetStatusStr(err),
                      err);
            return false;
        }
        return true;
    }

    template <typename T>
    bool
    LoadVar(const char* key,
            T& var,
            const T default_value)
    {
        if (auto err = WUPSStorageAPI::GetOrStoreDefault(key, var, default_value)) {
            LOG_ERROR("Failed to get or create %s: %s (%d)",
                      key,
                      WUPSStorageAPI_GetStatusStr(err),
                      err);
            return false;
        }
        return true;
    }

    bool
    MatchID(const AssetInfo& asset,
            const PrefGroup& grp)
    {
        if (!grp.ids)
            return false;
        if (grp.ids->contains("*"sv))
            return true;

        return asset.metadata
            && asset.metadata->themeID
            && grp.ids->contains(*asset.metadata->themeID);
    }

    bool
    MatchName(const std::string& name,
              const PrefGroup& grp)
    {
        if (!grp.names)
            return false;
        if (grp.names->contains("*"sv))
            return true;

        return grp.names->contains(name);
    }

    bool
    MatchTags(const AssetInfo& asset,
              const PrefGroup& grp)
        noexcept
    {
        if (!grp.tags)
            return false;
        if (grp.tags->contains("*"sv))
            return true;
        if (!asset.metadata || !asset.metadata->tags)
            return false;

        for (auto& tag : *asset.metadata->tags)
            if (grp.tags->contains(tag))
                return true;
        return false;
    }

    void
    OnShuffleBGMsChanged(wupsext::ConfigBooleanVarItemImpl &item)
    {
        LOG_VERBOSE("Setting %s changed: %s, restart needed.",
                    item.identifier.data(),
                    item.variable ? "true" : "false");
        using enum wupsext::ConfigToggleItemGroup::Mode;
        gBGMsGroup->setMode(item.variable ? Multi : Single);
        gNeedToRestart = true;
    }

    void
    OnShuffleFontsChanged(wupsext::ConfigBooleanVarItemImpl &item)
    {
        LOG_VERBOSE("Setting %s changed: %s, restart needed.",
                    item.identifier.data(),
                    item.variable ? "true" : "false");
        using enum wupsext::ConfigToggleItemGroup::Mode;
        gFontsGroup->setMode(item.variable ? Multi : Single);
        gNeedToRestart = true;
    }

    void
    OnShuffleThemesChanged(wupsext::ConfigBooleanVarItemImpl &item)
    {
        LOG_VERBOSE("Setting %s changed: %s",
                    item.identifier.data(),
                    item.variable ? "true" : "false");
        using enum wupsext::ConfigToggleItemGroup::Mode;
        gThemesGroup->setMode(item.variable ? Multi : Single);
        gNeedToRestart = true;
    }

    void
    PickAndApplyTheme()
    {
        gLoaded = {};

        if (!gCfg.themeManagerEnabled)
            return;

        if (gCfg.enabledThemes.empty())
            return;

        bool success = false;
        ReplaceParams args;
        std::string themeName;

        const auto& themes = GetScanResult().themes;

        if (!gCfg.mashupThemes) {
            themeName = PickRandomItem(gCfg.enabledThemes);
            auto it = themes.find(themeName);
            if (it == themes.end())
                return;
            const auto& theme = it->second;

            args.menPack             = theme.resolve(themes_root, theme.menPack);
            args.men2Pack            = theme.resolve(themes_root, theme.men2Pack);
            args.cafeBaristaMenBfsar = theme.resolve(themes_root, theme.cafeBaristaMenBfsar);
            args.contentExtraDir     = theme.resolve(themes_root, theme.dir);

            if (!gCfg.overrideBgm) {
                LOG_INFO("Using theme BGM preferences.");
                args.bgmBfstm = PickPreferredBGM(theme);
            } else {
                if (args.cafeBaristaMenBfsar.empty() || IsUsingBGM(args.cafeBaristaMenBfsar)) {
                    LOG_INFO("User override BGM");
                    args.bgmBfstm = PickAnyEnabledBGM();
                }
            }
        } else {
            /* Mashup
             *
             * We just make a flat list of all possible files, even using the empty string when
             * a theme doesn't have it. If the empty string is selected, the file is not
             * replaced.
             */
            PathVec everyMenPack;
            PathVec everyMen2Pack;
            PathVec everyCafeBaristaMenBfsar;

            for (const auto& themeName : gCfg.enabledThemes) {
                auto it = themes.find(themeName);
                if (it == themes.end())
                    continue;

                const auto& theme = it->second;
                everyMenPack.push_back(theme.resolve(themes_root, theme.menPack));
                everyMen2Pack.push_back(theme.resolve(themes_root, theme.men2Pack));
                everyCafeBaristaMenBfsar.push_back(
                    theme.resolve(themes_root, theme.cafeBaristaMenBfsar)
                );
            }

            args.contentExtraDir = themes_root / PickRandomItem(gCfg.enabledThemes);

            if (!everyMenPack.empty())
                args.menPack = PickRandomItem(everyMenPack);
            if (!everyMen2Pack.empty())
                args.men2Pack = PickRandomItem(everyMen2Pack);
            if (!everyCafeBaristaMenBfsar.empty())
                args.cafeBaristaMenBfsar = PickRandomItem(everyCafeBaristaMenBfsar);

            args.bgmBfstm = PickAnyEnabledBGM();
        }

        SetFallbacksIfNeeded(args);

        success = ReplaceContent(args);

        if (success) {

            if (!gCfg.mashupThemes) {
                gLoaded.theme       = themeName;
            } else {
                gLoaded.men         = args.menPack.parent_path().filename().native();
                gLoaded.men2        = args.men2Pack.parent_path().filename().native();
                gLoaded.cafeBarista = args.cafeBaristaMenBfsar.parent_path().filename().native();
                gLoaded.content     = args.contentExtraDir.filename().native();
            }

            // For BGM, show folder and file name.
            auto bgmRel = RemovePrefix(args.bgmBfstm, bgms_root);
            bgmRel.replace_extension();
            gLoaded.bgm = bgmRel.native();

            if (gCfg.showNotification) {

                if (NotificationModule_SetDefaultValue(
                        NOTIFICATION_MODULE_NOTIFICATION_TYPE_INFO,
                        NOTIFICATION_MODULE_DEFAULT_OPTION_DURATION_BEFORE_FADE_OUT,
                        12.0f))
                    return;

                if (NotificationModule_SetDefaultValue(
                        NOTIFICATION_MODULE_NOTIFICATION_TYPE_INFO,
                        NOTIFICATION_MODULE_DEFAULT_OPTION_KEEP_UNTIL_SHOWN,
                        true))
                    return;

                if (!gLoaded.content.empty()){
                    std::string text = "Content: " + gLoaded.content;
                    NotificationModule_AddInfoNotification(text.c_str());
                }

                if (!gLoaded.bgm.empty()) {
                    std::string text = "BGM: " + gLoaded.bgm;
                    NotificationModule_AddInfoNotification(text.c_str());
                }

                if (!gLoaded.cafeBarista.empty()){
                    std::string text = "cafe_barista_men.bfsar: " + gLoaded.cafeBarista;
                    NotificationModule_AddInfoNotification(text.c_str());
                }

                if (!gLoaded.men2.empty()){
                    std::string text = "Men2.pack: " + gLoaded.men2;
                    NotificationModule_AddInfoNotification(text.c_str());
                }

                if (!gLoaded.men.empty()){
                    std::string text = "Men.pack: " + gLoaded.men;
                    NotificationModule_AddInfoNotification(text.c_str());
                }

                if (!gLoaded.theme.empty()) {
                    std::string text = "Theme: " + gLoaded.theme;
                    NotificationModule_AddInfoNotification(text.c_str());
                }

            } // if (gShowNotification)

        } // if (success)
    }

    std::filesystem::path
    PickAnyEnabledAsset(AssetType type)
    {
        using enum AssetType;

        const auto& themes = GetScanResult().themes;

        const auto& enabledAssets = type == BGM ? gCfg.enabledBgms : gCfg.enabledFonts;

        const auto& assets = type == BGM ? GetScanResult().bgms : GetScanResult().fonts;

        const std::string assetPrefix = type == BGM ? "bgm:"s : "font:"s;
        const std::string themePrefix = "theme:"s;

        // NOTE: Random selection is done on enabled names first, then on files.
        StringVec names;

        // Collect names of all enabled assets.
        for (const auto& name : enabledAssets)
            if (assets.contains(name))
                names.push_back(assetPrefix + name);

        // Collect all themes that have asset files.
        for (const auto& name : gCfg.enabledThemes) {
            auto it = themes.find(name);
            if (it == themes.end())
                continue;
            auto& theme = it->second;

            if (type == BGM) {
                if (!theme.bundledBFSTMFiles.empty())
                    names.push_back(themePrefix + name);
            } else {
                if (!theme.bundledTTFFiles.empty())
                    names.push_back(themePrefix + name);
            }
        }

        if (names.empty())
            return {};

        auto pickedName = PickRandomItem(names);

        if (pickedName.starts_with(assetPrefix)) {
            auto assetName = pickedName.substr(assetPrefix.size());
            auto& asset = assets.at(assetName);
            if (asset.files.empty())
                return {};
            auto pickedFilename = PickRandomItem(asset.files);
            switch (type) {
                case BGM:
                    return asset.resolve(bgms_root, pickedFilename);
                case Font:
                    return asset.resolve(fonts_root, pickedFilename);
            }
        }

        if (pickedName.starts_with(themePrefix)) {
            auto themeName = pickedName.substr(themePrefix.size());
            auto& theme = themes.at(themeName);

            switch (type) {
                case BGM:
                    return theme.resolve(themes_root,
                                         PickRandomItem(theme.bundledBFSTMFiles));
                case Font:
                    return theme.resolve(themes_root,
                                         PickRandomItem(theme.bundledTTFFiles));
            }
        }

        return {};
    }

    std::filesystem::path
    PickAnyEnabledBGM()
    {
        return PickAnyEnabledAsset(AssetType::BGM);
    }

    std::filesystem::path
    PickAnyEnabledFont()
    {
        return PickAnyEnabledAsset(AssetType::Font);
    }

    std::filesystem::path
    PickFont()
    {
        if (gCfg.mashupThemes)
            return PickAnyEnabledFont();

        const auto& themes = GetScanResult().themes;
        const auto& themeName = gLoaded.theme;

        if (!themeName.empty()) {
            auto it = themes.find(themeName);
            if (it != themes.end()) {
                auto& theme = it->second;
                return PickPreferredFont(theme);
            }
            return {};
        }

        return PickAnyEnabledFont();
    }

    std::filesystem::path
    PickPreferredAsset(const ThemeInfo& theme,
                       AssetType type)
    {
        using utils::IgnoreCase::Equal;
        using enum AssetType;

        auto& bundledFiles = type == BGM
            ? theme.bundledBFSTMFiles
            : theme.bundledTTFFiles;

        const auto& assets = type == BGM
            ? GetScanResult().bgms
            : GetScanResult().fonts;

        const auto& enabledAssets = type == BGM
            ? gCfg.enabledBgms
            : gCfg.enabledFonts;

        const auto& specialAsset = type == BGM
            ? specialSilence
            : specialCafeStd;

        auto aPref = theme.getAssetPreferences(type);

        // If theme asked to not allow any asset, stop early.
        if (HasAvoidAll(aPref))
            return {};

        // NOTE: Random selection is done on enabled names first, then on files.
        StringSet names;

        // Bundled files are always included, unless explicitly excluded.
        for (auto& file : bundledFiles) {
            auto name = GetBundledFileName(theme, file);

            if (aPref &&
                aPref->avoid &&
                aPref->avoid->names &&
                aPref->avoid->names->contains(name))
                continue; // try next file

            LOG_VERBOSE("Adding name: %s", name.c_str());
            names.insert(std::move(name));
        }

        // Assets have to be explicitly included, but an exclusion takes precedence.
        for (auto& [name, asset] : assets) {
            if (!enabledAssets.contains(name))
                continue;

            LOG_VERBOSE("Processing asset: %s", name.c_str());

            if (aPref) {

                if (aPref->avoid) {
                    if (MatchID(asset, *aPref->avoid) ||
                        MatchName(name, *aPref->avoid) ||
                        MatchTags(asset, *aPref->avoid))
                        continue; // try next asset
                }

                if (aPref->prefer) {
                    if (MatchID(asset, *aPref->prefer) ||
                        MatchName(name, *aPref->prefer) ||
                        MatchTags(asset, *aPref->prefer)) {
                        names.insert(name);
                    }
                } else if (type == Font) {
                    // When no font preference exists, assume it allows all fonts
                    LOG_INFO("Adding \"%s\" because no font preference",
                             name.c_str());
                    names.insert(name);
                }

            } else if (type == Font) {
                // When no font preference exists, assume it allows all fonts
                LOG_INFO("Adding \"%s\" because no preference",
                         name.c_str());
                names.insert(name);
            }
        }

        // If specialAsset (silence, CafeStd) is preferred, include it too.
        if (aPref &&
            aPref->prefer &&
            aPref->prefer->names &&
            aPref->prefer->names->contains(specialAsset))
            names.insert(specialAsset);

        if (names.empty()) {
            LOG_INFO("no names to pick from (type == \"%s\")",
                     to_string(type).c_str());
            return {};
        }

        auto pickedName = PickRandomItem(names);
        LOG_INFO("pickedName = \"%s\"", pickedName.c_str());

        // If specialAsset (silence, CafeStd) was chosen, return it directly.
        if (pickedName == specialAsset)
            return pickedName;

        for (auto& file : bundledFiles) {
            auto name = GetBundledFileName(theme, file);
            if (Equal(name, pickedName))
                return theme.dir / file;
        }

        auto it = assets.find(pickedName);
        if (it == assets.end())
            return {};

        auto& asset = it->second;
        if(asset.files.empty())
            return {};

        const auto& assets_root = type == BGM
            ? bgms_root
            : fonts_root;

        return asset.resolve(assets_root, PickRandomItem(asset.files));
    }

    std::filesystem::path
    PickPreferredBGM(const ThemeInfo& theme)
    {
        auto result = PickPreferredAsset(theme, AssetType::BGM);
        if (utils::IgnoreCase::Equal(result, specialSilence))
            result = silence_bfstm_path;
        return result;
    }

    std::filesystem::path
    PickPreferredFont(const ThemeInfo& theme)
    {
        return PickPreferredAsset(theme, AssetType::Font);
    }

    template <std::ranges::forward_range R>
    std::ranges::range_value_t<R>
    PickRandomItem(R&& input)
    {
        using T = std::ranges::range_value_t<R>;

        if (input.empty()) [[unlikely]]
            throw std::logic_error{"Cannot select item from empty set!"};

        // Trivial case: avoid using the PRNG engine.
        if (input.size() == 1)
            return *input.begin();

        T result;
        std::ranges::sample(std::forward<R>(input),
                            &result,
                            1,
                            gPRNGEngine);
        return result;
    }

    AssetInfo
    ReadBGMInfo(const std::filesystem::directory_entry& entry,
                std::string& workBuffer)
    {
        AssetInfo bgm;

        if (entry.is_regular_file()) {

            if (!IsBFSTM(entry))
                throw std::logic_error{"not BGM: "s + entry.path().native()};

            bgm.files.push_back(entry.path().filename());

        } else if (entry.is_directory()) {

            bgm.dir = entry.path().filename();

            bgm.metadata = ReadMetadata(entry.path() / "metadata.json", workBuffer);

            for (const auto& childEntry : std::filesystem::directory_iterator{entry}) {
                if (!childEntry.is_regular_file())
                    continue;
                if (!IsBFSTM(childEntry))
                    continue;
                bgm.files.push_back(childEntry.path().filename());
            }

        } else {

            throw std::runtime_error{
                "invalid directory entry: \""s + entry.path().native() + "\""s
            };

        }

        return bgm;
    }

#ifdef ENABLE_CACHE

    bool
    ReadCache(ScanResult& result,
              std::string& workBuffer)
        noexcept
    {
        try {
            if (exists(cache_path)) {
                Cache cache;

                auto size = file_size(cache_path);
                workBuffer.resize(size);
                {
                    utils::File f{cache_path, "rb"};
                    auto r = f.read(workBuffer);
                    workBuffer.resize(r);
                }
                glz::ex::read<CacheOptions{}>(cache, workBuffer);

                cache.checkVersion();
                result = std::move(cache.result);
                LOG_INFO("Loaded cache.");
                return true;
            } else {
                return false;
            }
        }
        catch (std::exception& e){
            LOG_ERROR("Failed to load cache: %s", e.what());
            return false;
        }
    }

#endif // ENABLE_CACHE

    AssetInfo
    ReadFontInfo(const std::filesystem::directory_entry& entry,
                 std::string& workBuffer)
    {
        AssetInfo font;

        if (entry.is_regular_file()) {

            if (!IsTTF(entry))
                throw std::logic_error{"not TTF: "s + entry.path().native()};

            font.files.push_back(entry.path().filename());

        } else if (entry.is_directory()) {

            font.dir = entry.path().filename();

            font.metadata = ReadMetadata(entry.path() / "metadata.json", workBuffer);

            for (const auto& childEntry : std::filesystem::directory_iterator{entry}) {
                if (!childEntry.is_regular_file())
                    continue;
                if (!IsTTF(childEntry))
                    continue;
                font.files.push_back(childEntry.path().filename());
            }

        } else {

            throw std::runtime_error{
                "invalid directory entry: \""s + entry.path().native() + "\""s
            };

        }

        return font;
    }

    std::optional<Metadata>
    ReadMetadata(const std::filesystem::path& metaPath,
                 std::string& workBuffer)
    {
        try {
            if (!exists(metaPath))
                return {};

            MetadataJson mj;
            glz::ex::read_file_json<JsonReadOptions{}>(mj, metaPath.native(), workBuffer);
            return { std::move(mj.metadata) };
        }
        catch (std::exception& e) {
            LOG_ERROR("Error reading metadata \"%s\": %s",
                      metaPath.c_str(),
                      e.what());
            return {};
        }
    }

    ThemeInfo
    ReadThemeInfo(const std::filesystem::directory_entry& themeEntry,
                  std::string& workBuffer)
    {
        ThemeInfo theme;
        theme.dir = themeEntry.path().filename();
        theme.metadata = ReadMetadata(themeEntry.path() / "metadata.json", workBuffer);

        // Check for special files
        using utils::IgnoreCase::Equal;
        try {
            for (const auto& childEntry :
                     std::filesystem::recursive_directory_iterator{themeEntry}) {

                if (!childEntry.is_regular_file())
                    continue;

                auto relPath = RemovePrefix(childEntry, themeEntry);
                auto filename = childEntry.path().filename();
                auto ext = filename.extension().native();
                auto name = filename.stem().native();

                if (theme.menPack.empty()
                    && Equal(filename, "Men.pack"sv)) {
                    theme.menPack = relPath;
                }

                if (theme.men2Pack.empty()
                    && Equal(filename, "Men2.pack"sv)) {
                    theme.men2Pack = relPath;
                }

                if (theme.cafeBaristaMenBfsar.empty()
                    && Equal(filename, "cafe_barista_men.bfsar"sv)) {
                    theme.cafeBaristaMenBfsar = relPath;
                }

                if (Equal(filename, ".bfstm"sv))
                    theme.bundledBFSTMFiles.push_back(relPath);

                if (Equal(ext, ".ttf"sv))
                    theme.bundledTTFFiles.push_back(relPath);


                if (Equal(filename, "preferences.json"sv)) {
                    try {
                        Preferences p;
                        glz::ex::read_file_json(p, childEntry.path().native(), workBuffer);
                        theme.preferences = std::move(p);
                    }
                    catch (std::exception& e) {
                        LOG_ERROR("Failed to parse preferences.json: %s", e.what());
                    }

                }
            }

        }
        catch (std::exception& e) {
            LOG_ERROR("Error iterating directory \"%s\": %s",
                      themeEntry.path().c_str(),
                      e.what());
        }

        return theme;
    }

    void
    ReloadConfig()
    {
        LoadVar(KEY_THEME_MANAGER_ENABLED,
                gCfg.themeManagerEnabled,
                DEFAULT_THEME_MANAGER_ENABLED);

        LoadVar(KEY_SHOW_NOTIFICATION,
                gCfg.showNotification,
                DEFAULT_SHOW_NOTIFICATION);

        // NOTE: let's migrate from "suffleThemes" to "shuffleThemes"
        bool suffleThemes;
        if (LoadVar(KEY_SHUFFLE_THEMES_OLD, suffleThemes)) {
            // If we found "suffleThemes", transfer it to gShuffleThemes and delete the old key.
            gCfg.shuffleThemes = suffleThemes;
            StoreVar(KEY_SHUFFLE_THEMES, gCfg.shuffleThemes);
            WUPSStorageAPI::DeleteItem(KEY_SHUFFLE_THEMES_OLD);
        } else {
            // "suffleThemes" not found, already migrated
            LoadVar(KEY_SHUFFLE_THEMES,
                    gCfg.shuffleThemes,
                    DEFAULT_SHUFFLE_THEMES);
        }

        LoadVar(KEY_MASHUP_THEMES,
                gCfg.mashupThemes,
                DEFAULT_MASHUP_THEMES);

        gCfg.enabledThemes.clear();
        {
            std::string rawEnabledThemes;
            if (LoadVar(KEY_ENABLED_THEMES,
                        rawEnabledThemes,
                        ""s))
                gCfg.enabledThemes = SplitString(rawEnabledThemes);
        }

        LoadVar(KEY_OVERRIDE_BGM,
                gCfg.overrideBgm,
                DEFAULT_OVERRIDE_BGM);

        LoadVar(KEY_SHUFFLE_BGMS,
                gCfg.shuffleBgms,
                DEFAULT_SHUFFLE_BGMS);

        gCfg.enabledBgms.clear();
        {
            std::string rawEnabledBgms;
            if (LoadVar(KEY_ENABLED_BGMS,
                        rawEnabledBgms,
                        ""s))
                gCfg.enabledBgms = SplitString(rawEnabledBgms);
        }

        LoadVar(KEY_FONTS_ENABLED,
                gCfg.fontsEnabled,
                DEFAULT_FONTS_ENABLED);

        LoadVar(KEY_SHUFFLE_FONTS,
                gCfg.shuffleFonts,
                DEFAULT_SHUFFLE_FONTS);

        LoadVar(KEY_GLOBAL_FONT,
                gCfg.globalFont,
                DEFAULT_GLOBAL_FONT);

        gCfg.enabledFonts.clear();
        {
            std::string rawEnabledFonts;
            if (LoadVar(KEY_ENABLED_FONTS,
                        rawEnabledFonts,
                        ""s))
                gCfg.enabledFonts = SplitString(rawEnabledFonts);
        }

        LOG_VERBOSE("Finished reloading config.");
    }

    std::filesystem::path
    RemovePrefix(const std::filesystem::path& input,
                 const std::filesystem::path& prefix)
    {
        auto [inputIt, prefixIt] = std::ranges::mismatch(input, prefix);
        if (prefixIt == prefix.end()) {
            std::filesystem::path result;
            for (; inputIt != input.end(); ++inputIt)
                result /= *inputIt;
            return result;
        }
        return input;
    }

    ScanResult
    ScanFiles()
        noexcept
    {
        using std::filesystem::directory_iterator;

        utils::Timer timer;

        ScanResult result;
        std::string workBuffer;

#ifdef ENABLE_CACHE
        gCacheWriteThread = {}; // Make sure there's no background thread writing to it.
        if (ReadCache(result, workBuffer))
                goto return_result;
#endif

        // Find all theme folders in SD:/wiiu/themes/
        try {
            if (exists(themes_root)) {
                for (const auto &themeEntry : directory_iterator{themes_root}) {
                    if (!themeEntry.is_directory())
                        continue;
                    auto theme = ReadThemeInfo(themeEntry, workBuffer);
                    auto name = theme.getName();
                    result.themes.emplace(name, std::move(theme));
                }
            }
        }
        catch (std::exception &e) {
            LOG_WARN("Failed themes scan: %s", e.what());
        }

        try {
            // Find all BGMs in SD:/wiiu/bgms/, including sub-directories.
            if (exists(bgms_root)) {
                for (const auto &entry : directory_iterator{bgms_root}) {
                    if (entry.is_regular_file()) {
                        if (!IsBFSTM(entry))
                            continue;
                        // Ignore silence.bfstm
                        if (utils::IgnoreCase::Equal(entry.path().filename(),
                                                     silence_bfstm_path.filename()))
                            continue;
                    }

                    try {
                        auto bgm = ReadBGMInfo(entry, workBuffer);
                        auto name = bgm.getName();
                        result.bgms.emplace(name, std::move(bgm));
                    }
                    catch (std::exception& e) {
                        LOG_ERROR("Failed scanning \"%s\": %s",
                                  entry.path().c_str(),
                                  e.what());
                    }
                }
            }
        }
        catch (std::exception& e) {
            LOG_WARN("Failed BGMs scan: %s", e.what());
        }

        try {
            // Find all fonts in SD:/wiiu/fonts/, including sub-directories
            if (exists(fonts_root)) {
                for (const auto &entry : directory_iterator{fonts_root}) {
                    if (entry.is_regular_file())
                        if (!IsTTF(entry))
                            continue;
                    try {
                        auto font = ReadFontInfo(entry, workBuffer);
                        auto name = font.getName();
                        result.fonts.emplace(name, std::move(font));
                    }
                    catch (std::exception& e) {
                        LOG_ERROR("Failed scanning \"%s\": %s",
                                  entry.path().c_str(),
                                  e.what());
                    }
                }
            }
        }
        catch (std::exception &e) {
            LOG_WARN("Failed fonts scan: %s", e.what());
        }

#ifdef ENABLE_CACHE
        // Write the cache in a background thread.
        gCacheWriteThread = std::jthread{WriteCache, result};

    return_result:
#endif

        auto totalTime = duration_cast<std::chrono::milliseconds>(timer.getElapsed());
        auto timeStr = std::to_string(totalTime.count());
        LOG_INFO("File scan took %s ms", timeStr.c_str());

        LOG_INFO("Found %u themes, %u bgms, %u fonts.",
                 static_cast<unsigned>(result.themes.size()),
                 static_cast<unsigned>(result.bgms.size()),
                 static_cast<unsigned>(result.fonts.size()));

        return result;
    }

    void
    SetFallbacksIfNeeded(ReplaceParams& args)
    {
        // If there's a .bfstm, and no .bfsar, use BGM.bfsar.
        if (!args.bgmBfstm.empty() && args.cafeBaristaMenBfsar.empty()) {
            if (!exists(bgm_bfsar_path))
                LOG_WARN("\"%s\" not found!", bgm_bfsar_path.c_str());
            else
                args.cafeBaristaMenBfsar = bgm_bfsar_path;
        }

        // If the .bfsar needs BGM.bfstm, but there's no .bfstm, use silence.bfstm.
        if (IsUsingBGM(args.cafeBaristaMenBfsar) && args.bgmBfstm.empty()) {
            LOG_INFO("BFSAR seems to be using BGM, but has no BGM.bfstm");
            if (!exists(silence_bfstm_path)) {
                LOG_ERROR("\"%s\" not found, disabling .bfsar replacement!",
                          silence_bfstm_path.c_str());
                args.cafeBaristaMenBfsar = ""; // use stock cafe_barista_men.bfsar
            } else
                args.bgmBfstm = silence_bfstm_path;
        }
    }

    StringSet
    SplitString(const std::string& input)
    {
        StringSet result;
        for (const auto token : std::views::split(input, "|"s))
            if (!token.empty())
                result.insert(std::string{token.begin(), token.end()});
        return result;
    }

    template <typename T>
    void
    StoreVar(const char* key,
             const T& var)
    {
        if (auto err = WUPSStorageAPI::Store(key, var)) {
            LOG_ERROR("Failed to store %s: %s (%d)",
                      key,
                      WUPSStorageAPI_GetStatusStr(err),
                      err);
        }
    }

    std::string
    to_string(AssetType type)
    {
        switch (type) {
            using enum AssetType;

            case BGM:
                return "BGM";

            case Font:
                return "Font";

            default:
                return "<ERROR>";
        }
    }

#ifdef ENABLE_CACHE

    void
    WriteCache(std::stop_token stoken,
               ScanResult result)
        noexcept
    {
        try {
            Cache cache;
            if (stoken.stop_requested())
                return;

            cache.result = std::move(result);

            if (stoken.stop_requested())
                return;

            std::vector<std::byte> output;
            glz::ex::write<CacheOptions{}>(cache, output);

            if (stoken.stop_requested())
                return;

            utils::Timer writeTimer;

            {
                utils::File f{cache_path, "wb"};
                if (stoken.stop_requested())
                    return;
                f.write(output);
            }

            if (stoken.stop_requested())
                return;

            auto elapsed = duration_cast<std::chrono::milliseconds>(
                writeTimer.getElapsed()
            );
            auto timeStr = std::to_string(elapsed.count());
            LOG_INFO("Time to write cache: %s ms", timeStr.c_str());
        }
        catch (std::exception& e) {
            LOG_ERROR("Error writing cache: %s", e.what());
        }
    }

#endif // ENABLE_CACHE

} // namepace

WUPSConfigAPICallbackStatus
ConfigMenuOpenedCallback(WUPSConfigCategoryHandle rootHandle)
    noexcept
{
    try {

        // Copy all presistent settings, so we can check if anything changed later.
        gOldCfg = gCfg;

        WUPSStorageAPI::ForceReloadStorage();
        ReloadConfig();

        WUPSConfigCategory root = WUPSConfigCategory(rootHandle);

        using BoolItem = wupsext::ConfigBooleanVarItem;

        root.add(BoolItem{"Enable themes",
                          KEY_THEME_MANAGER_ENABLED,
                          gCfg.themeManagerEnabled});

        root.add(BoolItem{"Show theme notification",
                          KEY_SHOW_NOTIFICATION,
                          gCfg.showNotification});

        root.add(BoolItem{"Shuffle themes",
                          KEY_SHUFFLE_THEMES,
                          gCfg.shuffleThemes,
                          OnShuffleThemesChanged});

        root.add(BoolItem{"Mashup themes",
                          KEY_MASHUP_THEMES,
                          gCfg.mashupThemes});

        root.add(BoolItem{"Override theme BGM",
                          KEY_OVERRIDE_BGM,
                          gCfg.overrideBgm});

        root.add(BoolItem{"Shuffle BGMs",
                          KEY_SHUFFLE_BGMS,
                          gCfg.shuffleBgms,
                          OnShuffleBGMsChanged});

        root.add(BoolItem{"Enable fonts",
                          KEY_FONTS_ENABLED,
                          gCfg.fontsEnabled});

        root.add(BoolItem{"Shuffle fonts",
                          KEY_SHUFFLE_FONTS,
                          gCfg.shuffleFonts,
                          OnShuffleFontsChanged});

        root.add(BoolItem{"Use custom font outside Wii U Menu",
                          KEY_GLOBAL_FONT,
                          gCfg.globalFont});

        // Show last loaded theme
        if (!gLoaded.theme.empty())
            root.add(WUPSConfigItemStub::Create("Loaded theme: " + gLoaded.theme));

        if (!gLoaded.content.empty())
            root.add(WUPSConfigItemStub::Create("Loaded Content: " + gLoaded.content));

        if (!gLoaded.men.empty())
            root.add(WUPSConfigItemStub::Create("Loaded Men: " + gLoaded.men));

        if (!gLoaded.men2.empty())
            root.add(WUPSConfigItemStub::Create("Loaded Men2: " + gLoaded.men2));

        if (!gLoaded.cafeBarista.empty())
            root.add(WUPSConfigItemStub::Create("Loaded cafe_barista_men: "
                                                + gLoaded.cafeBarista));

        if (!gLoaded.bgm.empty())
            root.add(WUPSConfigItemStub::Create("Loaded BGM: " + gLoaded.bgm));

        if (!gLoaded.font.empty())
            root.add(WUPSConfigItemStub::Create("Loaded font: " + gLoaded.font));

        // Now build categories for themes and fonts
        using enum wupsext::ConfigToggleItemGroup::Mode;
        using std::filesystem::directory_iterator;

        auto& [themes, bgms, fonts] = GetScanResult();

        gThemesGroup.emplace();
        auto themes_cat = WUPSConfigCategory::Create("Available themes...");
        for (const auto& [name, theme] : themes) {
            bool isEnabled = gCfg.enabledThemes.contains(name);
            themes_cat.add(
                wupsext::ConfigToggleItem{
                    name,
                    name,
                    isEnabled,
                    {},
                    &*gThemesGroup
                }
            );
        }
        gThemesGroup->setMode(gCfg.shuffleThemes ? Multi : Single);
        root.add(std::move(themes_cat));

        gBGMsGroup.emplace();
        auto bgms_cat = WUPSConfigCategory::Create("Available BGMs...");
        for (const auto& [name, bgm] : bgms) {
            bool isEnabled = gCfg.enabledBgms.contains(name);
            bgms_cat.add(
                wupsext::ConfigToggleItem{
                    name,
                    name,
                    isEnabled,
                    {},
                    &*gBGMsGroup
                }
            );
        }
        gBGMsGroup->setMode(gCfg.shuffleBgms ? Multi : Single);
        root.add(std::move(bgms_cat));

        gFontsGroup.emplace();
        auto fonts_cat = WUPSConfigCategory::Create("Available fonts...");
        for (const auto& [name, font] : fonts) {
            bool isEnabled = gCfg.enabledFonts.contains(name);
            fonts_cat.add(
                wupsext::ConfigToggleItem{
                    name,
                    name,
                    isEnabled,
                    {},
                    &*gFontsGroup
                }
            );
        }
        gFontsGroup->setMode(gCfg.shuffleFonts ? Multi : Single);
        root.add(std::move(fonts_cat));

#ifdef ENABLE_CACHE
        gDeleteCacheOnExit = false;
        root.add(BoolItem{"Delete cache on exit",
                          "gDeleteCacheOnExit",
                          gDeleteCacheOnExit});
#endif

        return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
    }
    catch (std::exception &e) {
        LOG_ERROR("Exception: %s\n", e.what());
        return WUPSCONFIG_API_CALLBACK_RESULT_ERROR;
    }
}

void
ConfigMenuClosedCallback()
{
    // Update gEnabledThemes based on the enabled theme items.
    gCfg.enabledThemes.clear();
    gThemesGroup->forEach(
        [](wupsext::ConfigToggleItemImpl &item)
        {
            if (item.value) {
                LOG_VERBOSE("Enabled theme: %s", item.identifier.data());
                gCfg.enabledThemes.insert(item.identifier);
            }
        }
    );

    // Update gEnabledBgms based on the enabled BGM items.
    gCfg.enabledBgms.clear();
    gBGMsGroup->forEach(
        [](wupsext::ConfigToggleItemImpl &item)
        {
            if (item.value) {
                LOG_VERBOSE("Enabled BGM: %s", item.identifier.data());
                gCfg.enabledBgms.insert(item.identifier);
            }
        }
    );

    // Updated gEnabledFonts based on the enabled font items.
    gCfg.enabledFonts.clear();
    gFontsGroup->forEach(
        [](wupsext::ConfigToggleItemImpl &item)
        {
            if (item.value) {
                LOG_VERBOSE("Enabled font: %s", item.identifier.data());
                gCfg.enabledFonts.insert(item.identifier);
            }
        }
    );

    gThemesGroup.reset();
    gBGMsGroup.reset();
    gFontsGroup.reset();

    if (!gCfg.shuffleThemes && gCfg.enabledThemes.size() > 1)
        LOG_WARN("Inconsistent state: "
                 "enabledThemes.size() > 1 but shuffleThemes is false!");

    if (!gCfg.shuffleBgms && gCfg.enabledBgms.size() > 1)
        LOG_WARN("Inconsistent state: "
                 "enabledBgms.size() > 1 but shuffleBgms is false!");


    if (!gCfg.shuffleFonts && gCfg.enabledFonts.size() > 1)
        LOG_WARN("Inconsistent state: "
                 "enabledFonts.size() > 1 but shuffleFonts is false!");

    StoreVar(KEY_THEME_MANAGER_ENABLED, gCfg.themeManagerEnabled);
    StoreVar(KEY_SHOW_NOTIFICATION,     gCfg.showNotification);

    StoreVar(KEY_SHUFFLE_THEMES,        gCfg.shuffleThemes);
    StoreVar(KEY_MASHUP_THEMES,         gCfg.mashupThemes);

    StoreVar(KEY_OVERRIDE_BGM,          gCfg.overrideBgm);

    StoreVar(KEY_FONTS_ENABLED,         gCfg.fontsEnabled);
    StoreVar(KEY_SHUFFLE_FONTS,         gCfg.shuffleFonts);
    StoreVar(KEY_GLOBAL_FONT,           gCfg.globalFont);

    StoreVar(KEY_ENABLED_THEMES, JoinStrings(gCfg.enabledThemes, gCfg.shuffleThemes));
    StoreVar(KEY_ENABLED_BGMS,   JoinStrings(gCfg.enabledBgms,   gCfg.shuffleBgms));
    StoreVar(KEY_ENABLED_FONTS,  JoinStrings(gCfg.enabledFonts,  gCfg.shuffleFonts));

    if (auto err = WUPSStorageAPI::SaveStorage()) {
        LOG_ERROR("Failed to save storage: %s (%d)",
                  WUPSStorageAPI_GetStatusStr(err),
                  err);
    }

#ifdef ENABLE_CACHE
    if (gDeleteCacheOnExit) {
        ClearScanResult();
        gNeedToRestart = true;
        try {
            gCacheWriteThread = {};
            if (exists(cache_path)) {
                remove(cache_path);
            }
        }
        catch (std::exception& e) {
            LOG_ERROR("Failed to delete cache: %s", e.what());
        }
    }
#endif

    if (ConfigChangeNeedsRestart(gCfg, *gOldCfg))
        gNeedToRestart = true;

    gOldCfg.reset();

    if (gNeedToRestart && CalledFromWiiUMenu()) {
        SYSLaunchMenu();
        gNeedToRestart = false;
    }
}

INITIALIZE_PLUGIN()
{
    InitializePRNG();

    if (auto err = ContentRedirection_InitLibrary()) {
        LOG_ERROR("Failed to init ContentRedirection. Error %s %d",
                  ContentRedirection_GetStatusStr(err),
                  err);
        OSFatal("Failed to init ContentRedirection.");
    }

    bool disableNotifications = false;
    if (NotificationModule_InitLibrary()) {
        LOG_ERROR("NotificationModule_InitLibrary failed");
        disableNotifications = true;
    }

    WUPSConfigAPIOptionsV1 configOptions = {.name = "StyleMiiU"};
    if (auto err = WUPSConfigAPI_Init(configOptions,
                                      ConfigMenuOpenedCallback,
                                      ConfigMenuClosedCallback)) {
        LOG_ERROR("Failed to init config api: %s (%d)",
                  WUPSConfigAPI_GetStatusStr(err),
                  err);
    }

    ReloadConfig();

    if (disableNotifications)
        gCfg.showNotification = false;

    if (auto err = WUPSStorageAPI::SaveStorage()) {
        LOG_ERROR("Failed to save storage: %s (%d)",
                  WUPSStorageAPI_GetStatusStr(err),
                  err);
    }

    gContentLayerHandle = 0;
}

DEINITIALIZE_PLUGIN()
{
#ifdef ENABLE_CACHE
    gCacheWriteThread = {};
#endif
    NotificationModule_DeInitLibrary();
}

ON_APPLICATION_START()
{
    initLogging();

    if (CalledFromWiiUMenu()) {
        WUPSStorageAPI::ForceReloadStorage();
        ReloadConfig();
        PickAndApplyTheme();
    }
}

ON_APPLICATION_ENDS()
{
#ifdef ENABLE_CACHE
    gCacheWriteThread = {};
#endif
    ClearScanResult();
    gFontStdBlob.reset();
    gLoaded.font.clear();

    if (gContentLayerHandle != 0) {
        ContentRedirection_RemoveFSLayer(gContentLayerHandle);
        gContentLayerHandle = 0;
    }

    deinitLogging();
}

DECL_FUNCTION(BOOL,
              OSGetSharedData,
              OSSharedDataType type,
              uint32_t arg,
              void** out_data,
              uint32_t* out_size)
{
    try {
        if (arg == 0xefface) {
            /*
             * Homebrew apps can pass 0xefface as the (otherwise ignored) argument, to indicate
             * they really desire the original Cafe fonts, and not a custom font.
             *
             * efface
             *
             * transitive verb: To cause to disappear (as anything impresses or inscribed upon a
             * surface) by rubbing out, striking out, etc.; to erase; to render illegible or
             * indiscernible.
             */
            arg = 0;

            goto real_function;
        }

        if (!gCfg.fontsEnabled)
            goto real_function;

        // Never replace the font in the WUPS config menu.
        if (CalledFromWUPSMenu())
            goto real_function;

        // If the user does not want the font outside the Wii U Menu
        if (!gCfg.globalFont) {

            // Avoid when not inside the Wii U Menu.
            if (!CalledFromWiiUMenu()) {
                LOG_VERBOSE("Not called from Wii U Menu");
                goto real_function;
            }

            // Avoid when using the on-screen keyboard inside the Wii U Menu.
            if (CalledFromWiiUMenuSWKBD()) {
                LOG_VERBOSE("Called from Wii U Menu swkbd");
                goto real_function;
            }

        }

        // Actually load the font.
        switch (type) {

            case OS_SHAREDDATATYPE_FONT_STANDARD:

                {
                    std::lock_guard guard{gFontStdBlobMutex};
                    if (!gFontStdBlob)
                        gFontStdBlob = LoadFont(PickFont());
                }

                // When the blob exists but is empty, we already tried to load, and failed.
                if (gFontStdBlob->empty())
                    goto real_function;

                if (out_data)
                    *out_data = gFontStdBlob->data();
                if (out_size)
                    *out_size = gFontStdBlob->size();

                return true;

            default:
                goto real_function;

        } // switch (type)
    }
    catch (std::exception& e) {
        LOG_ERROR("Exception in OSGetSharedData(): %s", e.what());
    }
    catch (...) {
        LOG_ERROR("Unknown exception in OSGetSharedData()");
    }

 real_function:
    return real_OSGetSharedData(type, arg, out_data, out_size);
}

WUPS_MUST_REPLACE_FOR_PROCESS(OSGetSharedData,
                              WUPS_LOADER_LIBRARY_COREINIT,
                              OSGetSharedData,
                              WUPS_FP_TARGET_PROCESS_ALL);
