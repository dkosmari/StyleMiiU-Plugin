#include "themeSelector.h"
#include "globals.h"
#include <content_redirection/redirection.h>
#include <string>
#include <utils/logger.h>

using namespace std::literals;

bool ReplaceContent(const ReplaceParams& args) {
    const char* layerName = "StyleMiiU /vol/content";

    bool result = false;

    if (!args.contentExtraDir.empty()) {
        const char* target = "/vol/content";
        auto e = ContentRedirection_AddFSLayerEx(&gContentLayerHandle,
                                                 layerName,
                                                 target,
                                                 args.contentExtraDir.c_str(),
                                                 FS_LAYER_TYPE_EX_MERGE_DIRECTORY);
        if (e)
            LOG_ERROR("Failed to redirect \"%s\" to \"%s\": %s",
                      target,
                      args.contentExtraDir.c_str(),
                      ContentRedirection_GetStatusStr(e));
        else
            result = true;
    }

    if (!args.menPack.empty()) {
        const char* target = "/vol/content/Common/Package/Men.pack";
        auto e = ContentRedirection_AddFSLayerEx(&gContentLayerHandle,
                                                 layerName,
                                                 target,
                                                 args.menPack.c_str(),
                                                 FS_LAYER_TYPE_EX_REPLACE_FILE);
        if (e)
            LOG_ERROR("Failed to redirect \"%s\" to \"%s\": %s",
                      target,
                      args.menPack.c_str(),
                      ContentRedirection_GetStatusStr(e));
        else
            result = true;
    }

    if (!args.men2Pack.empty()) {
        const char* target = "/vol/content/Common/Package/Men2.pack";
        auto e = ContentRedirection_AddFSLayerEx(&gContentLayerHandle,
                                                 layerName,
                                                 target,
                                                 args.men2Pack.c_str(),
                                                 FS_LAYER_TYPE_EX_REPLACE_FILE);
        if (e)
            LOG_ERROR("Failed to redirect \"%s\" to \"%s\": %s",
                      target,
                      args.men2Pack.c_str(),
                      ContentRedirection_GetStatusStr(e));
        else
            result = true;

    }

    if (!args.cafeBaristaMenBfsar.empty()) {
        const char* target = "/vol/content/Common/Sound/Men/cafe_barista_men.bfsar";
        auto e = ContentRedirection_AddFSLayerEx(&gContentLayerHandle,
                                                 layerName,
                                                 target,
                                                 args.cafeBaristaMenBfsar.c_str(),
                                                 FS_LAYER_TYPE_EX_REPLACE_FILE);
        if (e)
            LOG_ERROR("Failed to redirect \"%s\" to \"%s\": %s",
                      target,
                      args.cafeBaristaMenBfsar.c_str(),
                      ContentRedirection_GetStatusStr(e));
        else
            result = true;
    }

    if (!args.bgmBfstm.empty()) {
        const char* target = "/vol/content/Common/Sound/Men/BGM.bfstm";
        auto e = ContentRedirection_AddFSLayerEx(&gContentLayerHandle,
                                                 layerName,
                                                 target,
                                                 args.bgmBfstm.c_str(),
                                                 FS_LAYER_TYPE_EX_REPLACE_FILE);
        if (e)
            LOG_ERROR("Failed to redirect \"%s\" to \"%s\": %s",
                      target,
                      args.bgmBfstm.c_str(),
                      ContentRedirection_GetStatusStr(e));
        else
            result = true;
    }

    return result;
}
