#include "themeSelector.h"
#include "globals.h"
#include <content_redirection/redirection.h>
#include <string>
#include <utils/logger.h>

using namespace std::literals;

bool ReplaceContent(ReplaceParams& args) {
    const char* layerName = "StyleMiiU";

    bool result = false;

    if (!args.content.empty()) {
        const char* target = "/vol/content";
        auto e = ContentRedirection_AddFSLayerEx(&gContentLayerHandle,
                                                 layerName,
                                                 target,
                                                 args.content.c_str(),
                                                 FS_LAYER_TYPE_EX_MERGE_DIRECTORY);
        if (!e) {
            result = true;
            LOG_INFO("Merged: /vol/content with \"%s\"", args.content.c_str());
        } else {
            LOG_ERROR("Failed to redirect \"%s\" to \"%s\": %s",
                      target,
                      args.content.c_str(),
                      ContentRedirection_GetStatusStr(e));
            args.content.clear();
        }
    }

    if (!args.menPack.empty()) {
        const char* target = "/vol/content/Common/Package/Men.pack";
        auto e = ContentRedirection_AddFSLayerEx(&gContentLayerHandle,
                                                 layerName,
                                                 target,
                                                 args.menPack.c_str(),
                                                 FS_LAYER_TYPE_EX_REPLACE_FILE);
        if (!e) {
            result = true;
            LOG_INFO("Replaced Men.pack by \"%s\"", args.menPack.c_str());
        } else {
            LOG_ERROR("Failed to redirect \"%s\" to \"%s\": %s",
                      target,
                      args.menPack.c_str(),
                      ContentRedirection_GetStatusStr(e));
            args.menPack.clear();
        }
    }

    if (!args.men2Pack.empty()) {
        const char* target = "/vol/content/Common/Package/Men2.pack";
        auto e = ContentRedirection_AddFSLayerEx(&gContentLayerHandle,
                                                 layerName,
                                                 target,
                                                 args.men2Pack.c_str(),
                                                 FS_LAYER_TYPE_EX_REPLACE_FILE);
        if (!e) {
            result = true;
            LOG_INFO("Replaced Men2.pack by \"%s\"", args.men2Pack.c_str());
        } else {
            LOG_ERROR("Failed to redirect \"%s\" to \"%s\": %s",
                      target,
                      args.men2Pack.c_str(),
                      ContentRedirection_GetStatusStr(e));
            args.men2Pack.clear();
        }
    }

    if (!args.cafeBaristaMenBfsar.empty()) {
        const char* target = "/vol/content/Common/Sound/Men/cafe_barista_men.bfsar";
        auto e = ContentRedirection_AddFSLayerEx(&gContentLayerHandle,
                                                 layerName,
                                                 target,
                                                 args.cafeBaristaMenBfsar.c_str(),
                                                 FS_LAYER_TYPE_EX_REPLACE_FILE);
        if (!e) {
            result = true;
            LOG_INFO("Replaced cafe_barista_men.bfsar by \"%s\"",
                     args.cafeBaristaMenBfsar.c_str());
        } else {
            LOG_ERROR("Failed to redirect \"%s\" to \"%s\": %s",
                      target,
                      args.cafeBaristaMenBfsar.c_str(),
                      ContentRedirection_GetStatusStr(e));
            args.cafeBaristaMenBfsar.clear();
        }
    }

    if (!args.bgmBfstm.empty()) {
        const char* target = "/vol/content/Common/Sound/Men/BGM.bfstm";
        auto e = ContentRedirection_AddFSLayerEx(&gContentLayerHandle,
                                                 layerName,
                                                 target,
                                                 args.bgmBfstm.c_str(),
                                                 FS_LAYER_TYPE_EX_REPLACE_FILE);
        if (!e) {
            result = true;
            LOG_INFO("Injected BGM.bfstm from \"%s\"", args.bgmBfstm.c_str());
        } else {
            LOG_ERROR("Failed to redirect \"%s\" to \"%s\": %s",
                      target,
                      args.bgmBfstm.c_str(),
                      ContentRedirection_GetStatusStr(e));
            args.bgmBfstm.clear();
        }
    }

    return result;
}
