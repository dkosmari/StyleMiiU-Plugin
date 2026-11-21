#ifdef DEBUG

#include <whb/log_cafe.h>
#include <whb/log_module.h>
#include <whb/log_udp.h>

#include "logger.h"

namespace {
    bool moduleLogInit = false;
    bool cafeLogInit   = false;
    bool udpLogInit    = false;

}

void initLogging()
{
    if (!(moduleLogInit = WHBLogModuleInit())) {
        cafeLogInit = WHBLogCafeInit();
        udpLogInit  = WHBLogUdpInit();
    }
}

void deinitLogging()
{
    if (moduleLogInit) {
        WHBLogModuleDeinit();
        moduleLogInit = false;
    }
    if (cafeLogInit) {
        WHBLogCafeDeinit();
        cafeLogInit = false;
    }
    if (udpLogInit) {
        WHBLogUdpDeinit();
        udpLogInit = false;
    }
}

#else // DEBUG

#include "logger.h"

void initLogging()
{}

void deinitLogging()
{}

#endif // DEBUG
