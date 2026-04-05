#ifndef DRAGONBONES_JNIBRIDGE_H
#define DRAGONBONES_JNIBRIDGE_H

namespace dmDragonBones{
    void ScriptInit(struct lua_State* l);
    void ScriptUpdate(struct lua_State* l);
}

#endif //DRAGONBONES_JNIBRIDGE_H 