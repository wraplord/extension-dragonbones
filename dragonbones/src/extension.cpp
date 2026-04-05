#include <dmsdk/sdk.h>
#include "script_dragonbones.h"

static dmExtension::Result AppInitializeDragonBones(dmExtension::AppParams* params)
{
    return dmExtension::RESULT_OK;
}

static dmExtension::Result InitializeDragonBones(dmExtension::Params* params)
{
    dmDragonBones::ScriptInit(params->m_L);
    
    dmLogInfo("Registered DragonBones extension\n");
    return dmExtension::RESULT_OK;
}

static dmExtension::Result UpdateDragonBones(dmExtension::Params* params)
{
    dmDragonBones::ScriptUpdate(params->m_L);
    return dmExtension::RESULT_OK;
}

static dmExtension::Result AppFinalizeDragonBones(dmExtension::AppParams* params)
{
    return dmExtension::RESULT_OK;
}

static dmExtension::Result FinalizeDragonBones(dmExtension::Params* params)
{
    
    return dmExtension::RESULT_OK;
}


// DM_DECLARE_EXTENSION(symbol, name, app_init, app_final, init, update, on_event, final)
DM_DECLARE_EXTENSION(DragonBonesExt, "DragonBonesExt", AppInitializeDragonBones, AppFinalizeDragonBones, InitializeDragonBones, UpdateDragonBones, 0, FinalizeDragonBones);
