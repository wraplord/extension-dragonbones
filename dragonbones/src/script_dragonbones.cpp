#include <dmsdk/dlib/hash.h>
#include <dmsdk/dlib/message.h>
#include <dmsdk/dlib/vmath.h>
#include <dmsdk/gameobject/script.h>

//#include "DragonBonesBridge.h"

#include "dragonBones/DragonBonesHeaders.h"
#include "opengl/OpenGLFactory.h"
#include "opengl/OpenGLSlot.h"

#include <string>
#include <vector>
#include <cstring>
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"

#include <dmsdk/graphics/graphics.h>
#include <dmsdk/render/render.h>
#include <dmsdk/gameobject/component.h>
#include <dmsdk/gamesys/property.h>
#include <dmsdk/gamesys/render_constants.h>
#include <dmsdk/sdk.h>
#include <dmsdk/script/script.h>

 
namespace dmDragonBones
{

    struct JniBridgeInstance {
        dmGameObject::HComponent m_component = nullptr;

        dragonBones::DragonBones* dragonBones = nullptr;
        dragonBones::Armature* armature = nullptr;
        dragonBones::opengl::OpenGLFactory* factory = nullptr;
       
        //GLfloat
        float projectionMatrix[16];
    
        float viewportWidth  = 800.0f;
        float viewportHeight = 600.0f;

       dmScript::LuaCallbackInfo* event_cbk;

        // Armature transform
        float worldScale = 0.5f;
        float worldTranslateX = 0.0f;
        float worldTranslateY = 0.0f;

        // Buffers to hold data, loaded off the GL thread
        std::vector<char> dragonBonesDataBuffer;
        std::vector<char> textureJsonBuffer;
        std::vector<char> texturePngDataBuffer;
        
        // State flags to handle race condition between surface creation and data loading
        bool isDataLoaded = false;
        bool isGlReady = true;

        dmArray<dmScript::LuaHBuffer> buffers;
        const std::function<void (dragonBones::EventObject *)> listener;

        ~JniBridgeInstance() {
            if (dragonBones) {
                delete dragonBones;
                dragonBones = nullptr;
            }
            if (factory) {
                delete factory;
                factory = nullptr;
            }
            
            if(event_cbk){
                dmScript::DestroyCallback(event_cbk);
            } 
            /*
            if (programId) {
                glDeleteProgram(programId);
                programId = 0;
            }
            */
        }
    };

    void _tryBuildArmature(JniBridgeInstance* instance); // Forward declaration

    // 辅助函数：创建正交投影矩阵
    void createOrthographicMatrix(float left, float right, float bottom, float top, float _near, float _far, float* matrix) {
        // 列主序矩阵
        matrix[0] = 2.0f / (right - left);
        matrix[1] = 0.0f;
        matrix[2] = 0.0f;
        matrix[3] = 0.0f;
        
        matrix[4] = 0.0f;
        matrix[5] = 2.0f / (top - bottom);
        matrix[6] = 0.0f;
        matrix[7] = 0.0f;
        
        matrix[8]  = 0.0f;
        matrix[9]  = 0.0f;
        matrix[10] = -2.0f / (_far - _near);
        matrix[11] =  0.0f;
        
        matrix[12] = -(right + left) / (right - left);
        matrix[13] = -(top + bottom) / (top - bottom);
        matrix[14] = -(_far + _near) / (_far - _near);

        matrix[15] = 1.0f;
    }
    
    void createIdentityMatrix(float* matrix) {
        matrix[0] = 1.0f; matrix[4] = 0.0f; matrix[8] = 0.0f; matrix[12] = 0.0f;
        matrix[1] = 0.0f; matrix[5] = 1.0f; matrix[9] = 0.0f; matrix[13] = 0.0f;
        matrix[2] = 0.0f; matrix[6] = 0.0f; matrix[10] = 1.0f; matrix[14] = 0.0f;
        matrix[3] = 0.0f; matrix[7] = 0.0f; matrix[11] = 0.0f; matrix[15] = 1.0f;
    }

    void createTranslateMatrix(float* matrix, float tx, float ty, float tz) {
        createIdentityMatrix(matrix);
        matrix[12] = tx;
        matrix[13] = ty;
        matrix[14] = tz;
    }
    
    void createScaleMatrix(float* matrix, float sx, float sy, float sz) {
        createIdentityMatrix(matrix);
        matrix[0] = sx;
        matrix[5] = sy;
        matrix[10] = sz;
    }
    
    // 辅助函数：将 DragonBones 2D 矩阵转换为 OpenGL 4x4 矩阵
    void convertDBMatrixToGL(const dragonBones::Matrix& dbMatrix, float* glMatrix) {
        createIdentityMatrix(glMatrix);
        glMatrix[0] = dbMatrix.a;
        glMatrix[1] = dbMatrix.b;
        glMatrix[4] = dbMatrix.c;
        glMatrix[5] = dbMatrix.d;
        glMatrix[12] = dbMatrix.tx;
        glMatrix[13] = dbMatrix.ty;
    }

    // 辅助函数：矩阵乘法
    void multiplyMatrices(const float* a, const float* b, float* result) {
        float res[16];
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                res[j * 4 + i] = 0.0f;
                for (int k = 0; k < 4; k++) {
                    res[j * 4 + i] += a[k * 4 + i] * b[j * 4 + k];
                }
            }
        }
        memcpy(result, res, sizeof(res));
    }

    void _tryBuildArmature(JniBridgeInstance* instance) {
        if (!instance->isGlReady || !instance->isDataLoaded || instance->armature) {
            return;
        }

        //dmLogInfo("Creating armature on GL thread...");
        
        // The factory might have old data, clear it before parsing new data.
        instance->factory->clear();

        std::pair<void*, int> textureInfo = {instance->texturePngDataBuffer.data(), (int)instance->texturePngDataBuffer.size()};
        auto* textureAtlasData = instance->factory->parseTextureAtlasData(instance->textureJsonBuffer.data(), &textureInfo);
        if (!textureAtlasData) {
            dmLogError("Failed to parse texture atlas data.");
            return;
        }
        instance->factory->addTextureAtlasData(textureAtlasData);

        auto* dragonBonesData = instance->factory->parseDragonBonesData(instance->dragonBonesDataBuffer.data());
        if (!dragonBonesData) {
            dmLogError("Failed to parse DragonBones data.");
            return;
        }

        const auto& armatureNames = dragonBonesData->getArmatureNames();
        std::string armatureNameToBuild;
        if (!armatureNames.empty()) {
            bool dragonFound = false;
            for (const auto& name : armatureNames) {
                if (name == "Dragon") {
                    armatureNameToBuild = name;
                    dragonFound = true;
                    break;
                }
            }
            if (!dragonFound) {
                armatureNameToBuild = armatureNames[0]; 
            }
        }

        if (armatureNameToBuild.empty()) {
            dmLogError("No armatures found in DragonBones data.");
            return;
        }

        auto* armatureObject = instance->factory->buildArmature(armatureNameToBuild, "", "", dragonBonesData->name);
        if (armatureObject)
        {
            instance->armature = armatureObject;
            dmLogInfo("Armature '%s' built at %p, instance is %p", armatureNameToBuild.c_str(), instance->armature, instance);
            instance->dragonBones->getClock()->add(armatureObject);
            // Reset animation to force armature to setup pose.
            // This overrides any "defaultActions" in the data file (e.g., auto-playing an empty animation),
            // which can cause rendering issues for some models.

            armatureObject->getAnimation()->reset();
            //dmLogInfo("Armature building done.");
           
           
        } else {
            dmLogError("Failed to build armature '%s'.", armatureNameToBuild.c_str());
        }
    }

    static void eventListener(dragonBones::EventObject * eventObj){
        dmLogInfo("Callback fired.");
        dmLogInfo("Name %s", eventObj->name.c_str());
        //frameEventCallback(L, instance, eventObj);

        dmLogInfo("Event time: %f", eventObj->time);
    }

    /*# 
      Initialize Dragon Bones. Get userdata instance
      Destroy the instance by calling destroy function
    */
    static int init(lua_State* L)
    {
        DM_LUA_STACK_CHECK(L, 1);

        float width = (float)luaL_checknumber(L, 1);
        float height = (float)luaL_checknumber(L, 2);

        auto* instance = new JniBridgeInstance();
        if (!instance->factory) {
            instance->factory = new dragonBones::opengl::OpenGLFactory();
            
        }
        if (!instance->dragonBones) {
            instance->dragonBones = new dragonBones::DragonBones(instance->factory);
            instance->factory->setDragonBones(instance->dragonBones);
        }

        instance->viewportWidth = width;
        instance->viewportHeight = height;
        createOrthographicMatrix(0.0f, instance->viewportWidth, instance->viewportHeight, 0.0f, -1.0f, 1.0f, instance->projectionMatrix);

        
        dmLogInfo("DragonBones Initialized");

        lua_pushlightuserdata(L, instance);

        //instance->listener =  event_listener;

        return 1;
    }

    /*#
       Destroy in final
    */
    static int destroy(lua_State* L) {
        //char* skeleton_data, size_t skeleton_len, char* texture_json_data, int texture_json_len, char* texture_png_data, int texture_png_len
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        delete instance;
        return 1;
     }

    /*#
      Load armatures and textures. The parameters are as follows
      1. is the instance return from init.
      2,3. are skeleton and texture json
 
    */
    static int loadData2(lua_State* L) {
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);

        
        dmBuffer::HBuffer skeleton_data_buffer =  dmScript::CheckBufferUnpack(L, 2);
        uint8_t* skeleton_bytes = 0x0;
        uint32_t skeleton_len   = 0;
        dmBuffer::GetBytes(skeleton_data_buffer, (void**)&skeleton_bytes, &skeleton_len);
        //dmLogInfo("Skeleton no of bytes: %d", skeleton_len);

        dmBuffer::HBuffer texture_json_buffer =  dmScript::CheckBufferUnpack(L, 3);
        uint8_t* texture_json_bytes = 0x0;
        uint32_t texture_json_len   = 0;
        dmBuffer::GetBytes(texture_json_buffer, (void**)&texture_json_bytes, &texture_json_len);
        //dmLogInfo("Atlas json no of bytes: %d", texture_json_len);

        
        //texture is set in mesh component
        //dmScript::LuaHBuffer* texture_png_buffer =  dmScript::CheckBuffer(L, 4);
        //uint8_t* texture_png_bytes = 0x0;
        //uint32_t texture_png_len   = 0;
        //dmBuffer::GetBytes(texture_png_buffer->m_Buffer, (void**)&texture_png_bytes, &texture_png_len);
        
        
        if (!instance) {
            dmLogInfo("No instance, called init.")
            return -1;
        }

        // Clean up previous model if it exists
        if (instance->armature) {
            dmLogInfo("Cleaning up previous armature and factory data.");
            // 1. Dispose of the old armature object. This will queue it for cleanup on the next advanceTime().
            instance->armature->dispose();
            instance->armature = nullptr;

            // 2. Clear all parsed data (dragonbones data, texture atlas data) from the factory.
            // Recreating the factory is dangerous because the DragonBones instance holds a pointer
            // to it as an event manager. Clearing is the intended way to switch models.
            instance->factory->clear();
        }
        instance->isDataLoaded = false;
        instance->dragonBonesDataBuffer.clear();
        instance->textureJsonBuffer.clear();
        instance->texturePngDataBuffer.clear();

        //dmLogInfo("Buffering data from byte arrays...");

        
        // Skeleton data
        //jbyte* skeleton_bytes = env->GetByteArrayElements(skeleton_data, nullptr);
        //size_t skeleton_len = env->GetArrayLength(skeleton_data);
        instance->dragonBonesDataBuffer.assign(skeleton_bytes, skeleton_bytes + skeleton_len);
        //env->ReleaseByteArrayElements(skeleton_data, skeleton_bytes, JNI_ABORT);
        instance->dragonBonesDataBuffer.push_back('\0'); // Null-terminate for string parsing

        if(instance->dragonBonesDataBuffer.empty()){
            dmLogInfo("Empty dargon bones %d", instance->dragonBonesDataBuffer[0]);
        }

        
        // Texture JSON data
        //jbyte* texture_json_bytes = env->GetByteArrayElements(texture_json_data, nullptr);
        //jsize texture_json_len = env->GetArrayLength(texture_json_data);
        instance->textureJsonBuffer.assign(texture_json_bytes, texture_json_bytes + texture_json_len);
        //env->ReleaseByteArrayElements(texture_json_data, texture_json_bytes, JNI_ABORT);
        instance->textureJsonBuffer.push_back('\0'); // Null-terminate for string parsing

        // Texture PNG data
        //jbyte* texture_png_bytes = env->GetByteArrayElements(texture_png_data, nullptr);
        //jsize texture_png_len = env->GetArrayLength(texture_png_data);
        //instance->texturePngDataBuffer.assign(texture_png_bytes, texture_png_bytes + texture_png_len);
        //env->ReleaseByteArrayElements(texture_png_data, texture_png_bytes, JNI_ABORT);
        if(instance->textureJsonBuffer.empty()){
            dmLogInfo("Empty dragon bones %d", instance->textureJsonBuffer[0]);
        }

        
        if (instance->dragonBonesDataBuffer.empty() ||  instance->textureJsonBuffer.empty()) {
            dmLogError("Failed to copy one or more byte arrays.");
            instance->isDataLoaded = false;
        } else {
            //dmLogInfo("Data successfully buffered.");
            instance->isDataLoaded = true;
            _tryBuildArmature(instance);
        }

        
        return 1;
        
    }

    static int resize(lua_State* L) {
        JniBridgeInstance* instance     = (JniBridgeInstance*)lua_touserdata(L, 1);
       
        instance->viewportWidth = (float)luaL_checknumber(L, 2);
        instance->viewportHeight = (float)luaL_checknumber(L, 3);
            // Create the projection matrix to map pixel coordinates to screen space
        createOrthographicMatrix(0.0f, (float)instance->viewportWidth, (float)instance->viewportHeight, 0.0f, -1.0f, 1.0f, instance->projectionMatrix);
        return 1;
    }

    static void frameEventCallback(lua_State* L, dmScript::LuaCallbackInfo* event_cbk, const std::string& type, dragonBones::EventObject * eventObj){
       
        if (!dmScript::SetupCallback(event_cbk))
        {
            dmLogInfo("Error cannot create callback.");
            return;
        }

        lua_newtable(L);
        lua_pushstring(L, "type");
        lua_pushstring(L, eventObj->type.c_str());
        lua_settable(L, -3);

        lua_pushstring(L, "name");
        lua_pushstring(L, eventObj->name.c_str());
        lua_settable(L, -3);
 
        lua_pushstring(L, "frame");
        lua_pushnumber(L, eventObj->time);
        lua_settable(L, -3);

        
        if(eventObj->data){
            lua_pushstring(L, "ints");
            lua_newtable(L);
            std::vector<int> ints = eventObj->data->ints;
            for(int i = 0; i < ints.size(); i++){
                lua_pushnumber(L, i + 1);
                lua_pushnumber(L, ints[i]);
                lua_settable(L, -3);
            }
            lua_settable(L, -3);

            lua_pushstring(L, "floats");
            lua_newtable(L);
        
            std::vector<float> floats = eventObj->data->floats;
            for(int i = 0; i < floats.size(); i++){
                lua_pushnumber(L, i + 1);
                lua_pushnumber(L, floats[i]);
                lua_settable(L, -3);
            }
        
            lua_settable(L, -3);

            lua_pushstring(L, "strings");
            lua_newtable(L);
            
            std::vector<std::string> strings = eventObj->data->strings;
            for(int i = 0; i < strings.size(); i++){
                lua_pushnumber(L, i + 1);
                lua_pushstring(L, strings[i].c_str());
                lua_settable(L, -3);
            }
            lua_settable(L, -3);

        }
        
        dmScript::PCall(L, 2, 0); // self + # user arguments

        dmScript::TeardownCallback(event_cbk);
    }


    static int addEventListener(lua_State* L) {
        JniBridgeInstance* instance     = (JniBridgeInstance*)lua_touserdata(L, 1);
        //const char* event_name = luaL_checkstring(L, 2);
        instance->event_cbk = dmScript::CreateCallback(L, 2);

        if(!instance){
            dmLogInfo("No instance.");
            return 0;
        }

        //std::string name(event_name);
        instance->factory->setEventCallback([L, instance](const std::string& type, dragonBones::EventObject * eventObj){
            //dmLogInfo("Send to lua");
            frameEventCallback(L, instance->event_cbk, type,  eventObj);
        });
        //instance->factory->addDBEventListener(dragonBones::EventObject::FRAME_EVENT, [](dragonBones::EventObject * eventObj){
        //    dmLogInfo("Event %s called.", dragonBones::EventObject::FRAME_EVENT);
        //});
        
        //dmLogInfo("Callback added.");
        return 1;
    }

     static int removeEventListener(lua_State* L) {
        JniBridgeInstance* instance     = (JniBridgeInstance*)lua_touserdata(L, 1);
        if(!instance){
            dmLogInfo("No instance.");
            return 0;
        }
        instance->factory->disableEvents();
        return 1;
    }


    //creating the meshes
    static int getNoSlots(lua_State* L){
        JniBridgeInstance* instance     = (JniBridgeInstance*)lua_touserdata(L, 1);
        const auto& slots = instance->armature->getSlots();
        lua_pushinteger(L, slots.size()); 
        return 1;
    }

    //handle by lua gc
    static int freeBuffers(lua_State* L){
        //JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        //clear save buffers
        //for(int i = 0 ; i < instance->buffers.Size(); i++){
        //    dmBuffer::Destroy(instance->buffers[i].m_Buffer);
        //}

        //resources release handle by OWNER::RES
        return 1;
    }
    
    //return table 
    static int getBuffers(lua_State* L){
        //DM_LUA_STACK_CHECK( L, 1);
        JniBridgeInstance* instance     = (JniBridgeInstance*)lua_touserdata(L, 1);
        //freeBuffers(instance);
        const auto& slots = instance->armature->getSlots();
        

        //projection
        float viewMatrix[16], scaleM[16], transM[16];
        createScaleMatrix(scaleM, instance->worldScale, instance->worldScale, 1.0f);
        createTranslateMatrix(transM, (instance->viewportWidth / 2.0f) + instance->worldTranslateX, (instance->viewportHeight / 2.0f) + instance->worldTranslateY, 0.0f);
        multiplyMatrices(transM, scaleM, viewMatrix);

        //dmLogInfo("View matrix 0,1 : %f, %f", viewMatrix[0], viewMatrix[1])
        auto aabb = instance->armature->getArmatureData()->aabb;
        float x = instance->worldTranslateX -  aabb.x;
        float y = instance->worldTranslateY -  aabb.y;
        float aabb_array[6] = {
           x, y, -1, x + aabb.width, y + aabb.height, 1
        };
        
        //dmLogInfo("AABB: %f, %f, %f, %f", aabb_array[0], aabb_array[1], aabb_array[3], aabb_array[4]);
        lua_newtable(L);
        
        //instance->buffers.SetCapacity(slots.size() * 2);
       

        for (int slot_index = 0; slot_index <slots.size(); slot_index++){
            auto& slot  = slots[slot_index];
            auto* openglSlot = static_cast<dragonBones::opengl::OpenGLSlot*>(slot);
            const char* slot_name = slot->getName().c_str();
            auto slot_vertices = openglSlot->vertices;
            auto slot_indices = openglSlot->indices;
            auto indices = slot_indices.data();

            if (!slot) {
                dmLogInfo("onDrawFrame: Skipping null slot.");
                continue;
            }

            if (!slot->getVisible()) {
                dmLogInfo("onDrawFrame: Slot '%s' is not visible.", slot->getName().c_str());
                continue;
            }

            if (!slot->getDisplay()) {
                dmLogInfo("onDrawFrame: Slot '%s' has no display object.", slot->getName().c_str());
                continue;
            }
                    
            if (!openglSlot) {
                dmLogInfo("onDrawFrame: Slot '%s' could not be cast to OpenGLSlot.", slot->getName().c_str());
                continue;
            }

            if (openglSlot->vertices.empty() || openglSlot->indices.empty()) {
                dmLogInfo("onDrawFrame: Skipping slot '%s' due to empty buffers or texture ID 0 (vertices: %zu, indices: %zu)",
                    slot->getName().c_str(), openglSlot->vertices.size(), openglSlot->indices.size());
                continue;
            }


            //vertices
            /*
            dmBuffer::HBuffer buffer1 = 0x0;
            {
                
                auto* vertices = slot_vertices.data();

                const dmBuffer::StreamDeclaration streams_decl[] = {
                    {dmHashString64("a_position"), dmBuffer::VALUE_TYPE_FLOAT32, 3},
                    {dmHashString64("a_texCoord"), dmBuffer::VALUE_TYPE_FLOAT32, 2},
                    {dmHashString64("a_normal"),   dmBuffer::VALUE_TYPE_FLOAT32, 3},
                };
            
                dmBuffer::Result r = dmBuffer::Create(slot_vertices.size()/4, streams_decl, 3, &buffer1);
            
                if (r == dmBuffer::RESULT_OK) {
                   
                    dmBuffer::Result rm = dmBuffer::SetMetaData(buffer1, dmHashString32("AABB"), instance->aabb_array , 6, dmBuffer::VALUE_TYPE_FLOAT32);
                    if(rm != dmBuffer::RESULT_OK){
                        dmLogInfo("Cannot set %s AABB", slot_name);
                    }

                    float* positions = 0x0;
                    float* texCoord = 0x0;
                    float* normals = 0x0;

                    uint32_t components = 0;
                    uint32_t components2 = 0;
                    uint32_t components3 = 0;

                    uint32_t stride = 0;
                    uint32_t stride2 = 0;
                    uint32_t stride3 = 0;

                    uint32_t count;

                    dmBuffer::Result r1 = dmBuffer::GetStream(buffer1, dmHashString64("a_position"), (void**)&positions, &count, &components,  &stride);
                    dmBuffer::Result r2 = dmBuffer::GetStream(buffer1, dmHashString64("a_texCoord"), (void**)&texCoord,  &count, &components2, &stride2);
                    dmBuffer::Result r3 = dmBuffer::GetStream(buffer1, dmHashString64("a_normal"),   (void**)&normals,  &count, &components3, &stride3);

                    //dmLogInfo("Buffer Count, Components: %d, %d", count, components);
                    if (r1 == dmBuffer::RESULT_OK && r2 == dmBuffer::RESULT_OK) {
                        int offset = 0;
                        for(int i = 0; i < count; ++i){
                            /*for (int c = 0; c < components; ++c) {
                                auto pos = vertices[offset + c];
                                auto tex = vertices[offset + c + 2] ;

                                positions[c] = c == 0 ? pos : -pos ; //y axis is up in defold
                                texCoord[c]  = c == 0 ? tex : 1.0 - tex ; //flip tex coordinates
                            }/

                            //y axis is up in defold
                            auto pos_x =  vertices[offset + 0];
                            auto pos_y =  vertices[offset + 1];
                            positions[0] =  pos_x ; positions[1] =  pos_y ; positions[2] = 0; //2 = z axis
                            
                            //flip y tex coordinates
                            auto tex_x = vertices[offset + 0 + 2] ;
                            auto tex_y = 1.0 - vertices[offset + 1 + 2] ;
                            texCoord[0]  = tex_x ; texCoord[1]  = tex_y; 

                            normals[0] = 0; normals[1] = 0; normals[2] = 1;

                            positions += stride;
                            texCoord  += stride2;
                            normals   += stride3;

                            offset += 4;
                            //dmLogInfo("vertices for %s values : %f, %f", slot_name, vertices[i]);
                        }
                    } else {
                         dmLogInfo("Cannot get vertices' streams. ");
                    }
                   
                    //normals
                    /*
                        // Compute Vertex Normals
                        std::vector<dmVMath::Vector3> verticesNormal;
                        verticesNormal.resize(slot_vertices.size()/2);

                        for (int i = 0; i < slot_indices.size(); i += 3)
                        {
                            // Get the face normal y & x
                            auto slot1 = indices[i+1] * 4;//3 * 4
                            auto slot0 = indices[i] * 4; //1

                            auto vector1_x = vertices[slot1] - vertices[slot0]; //xs
                            auto vector1_y = vertices[slot1 + 1] - vertices[slot0 + 1];
                            dmLogInfo("Normal for 1: %d, %d, %f, %f", vector1_x, vector1_y);

                            auto slot2 = indices[i+2] * 4;

                            auto vector2_x = vertices[slot2] - vertices[slot0];
                            auto vector2_y = vertices[slot2 + 1] - vertices[slot0 + 1];

                            auto faceNormal = dmVMath::Cross(dmVMath::Vector3(vector1_x, vector1_y,1), dmVMath::Vector3(vector2_x, vector2_y, 1));
                            faceNormal = dmVMath::Normalize(faceNormal);
                            dmLogInfo("Normal %f, %f", faceNormal.getX(), faceNormal.getY());

                            // Add the face normal to the 3 vertices normal touching this face
                            verticesNormal[indices[i]]   += faceNormal;
                            verticesNormal[indices[i+1]] += faceNormal;
                            verticesNormal[indices[i+2]] += faceNormal;
                        }

                        // Normalize vertices normal
                        for (int i = 0; i < verticesNormal.size(); i++){
                            verticesNormal[i] =  dmVMath::Normalize(verticesNormal[i]);
                        }
                        float* strm_normals = 0x0;
                    
                        uint32_t components3 = 0;
                        uint32_t stride3 = 0;
                        uint32_t count3;

                        dmBuffer::Result r3 = dmBuffer::GetStream(buffer1, dmHashString64("a_normal"), (void**)&strm_normals, &count3, &components3, &stride3);
                    
                        if (r3 == dmBuffer::RESULT_OK ) {
                            for(int i = 0; i < count; ++i){
                                for (int c = 0; c < components; ++c) {
                                    strm_normals[c] = c == 0 ? verticesNormal[i].getX() : verticesNormal[i].getY() ;
                                }
                                
                                strm_normals += stride3;
                            } 
                        } else {
                            dmLogInfo("Cannot get normals' streams.");
                        }
                    /*
                } else {
                    dmLogInfo("Cannot copy vertices");
                }
                
            }
            */

            //vertices
            dmBuffer::HBuffer buffer_trilist = 0x0;
            std::vector<float> trilist;
            {
                
                auto* vertices1 = slot_vertices.data(); 
                trilist.resize(slot_indices.size() * 4); //positions(x,y):2 and texture(u,v):2 , position.z = 1
                int trilist_offset = 0;
                for (int i = 0; i < slot_indices.size() ; i++){
                    unsigned short index = slot_indices[i];
                    int sno = index * 4;
                    float vx = vertices1[sno + 0]; float vy = vertices1[sno + 1]; //positions
                    float tx = vertices1[sno + 2]; float ty = vertices1[sno + 3]; //texture coord
                    trilist[trilist_offset + 0] = vx; trilist[trilist_offset + 1] = vy; 
                    trilist[trilist_offset + 2] = tx; trilist[trilist_offset + 3] = ty; 

                    trilist_offset += 4;
                }

                const dmBuffer::StreamDeclaration streams_decl[] = {
                    {dmHashString64("a_position"), dmBuffer::VALUE_TYPE_FLOAT32, 3},
                    {dmHashString64("a_texCoord"), dmBuffer::VALUE_TYPE_FLOAT32, 2},
                    {dmHashString64("a_normal"),   dmBuffer::VALUE_TYPE_FLOAT32, 3},
                };
            
                dmBuffer::Result r = dmBuffer::Create(trilist.size()/4, streams_decl, 3, &buffer_trilist);
            
                if (r == dmBuffer::RESULT_OK) {
                    
                      

                    dmBuffer::Result rm = dmBuffer::SetMetaData(buffer_trilist, dmHashString32("AABB"), &aabb_array , 6, dmBuffer::VALUE_TYPE_FLOAT32);
                    if(rm != dmBuffer::RESULT_OK){
                        dmLogInfo("Cannot set %s AABB", slot_name);
                    }

                    float* positions = 0x0;
                    float* texCoord = 0x0;
                    float* normals = 0x0;

                    uint32_t components = 0;
                    uint32_t components2 = 0;
                    uint32_t components3 = 0;

                    uint32_t stride = 0;
                    uint32_t stride2 = 0;
                    uint32_t stride3 = 0;

                    uint32_t count;

                    dmBuffer::Result r1 = dmBuffer::GetStream(buffer_trilist, dmHashString64("a_position"), (void**)&positions, &count, &components,  &stride);
                    dmBuffer::Result r2 = dmBuffer::GetStream(buffer_trilist, dmHashString64("a_texCoord"), (void**)&texCoord,  &count, &components2, &stride2);
                    dmBuffer::Result r3 = dmBuffer::GetStream(buffer_trilist, dmHashString64("a_normal"),   (void**)&normals,  &count, &components3, &stride3);

                    //dmLogInfo("Buffer Count, Components: %d, %d", count, components);
                    if (r1 == dmBuffer::RESULT_OK && r2 == dmBuffer::RESULT_OK) {
                        int offset = 0;
                        for(int i = 0; i < count; ++i){
                           

                            //y axis is up in defold
                            auto pos_x =  trilist[offset + 0];
                            auto pos_y =  trilist[offset + 1];
                            positions[0] =  pos_x ; positions[1] =  pos_y ; positions[2] = 0; //2 = z axis
                            
                            //flip y tex coordinates
                            auto tex_x = trilist[offset + 0 + 2] ;
                            auto tex_y = 1.0 - trilist[offset + 1 + 2] ;
                            texCoord[0]  = tex_x ; texCoord[1]  = tex_y; 

                            normals[0] = 0; normals[1] = 0; normals[2] = 1;

                            positions += stride;
                            texCoord  += stride2;
                            normals   += stride3;

                            offset += 4;
                            //dmLogInfo("vertices for %s values : %f, %f", slot_name, vertices[i]);
                        }
                    } else {
                         dmLogInfo("Cannot get trilist vertices' streams. ");
                    }
                   
                   
                } else {
                    dmLogInfo("Cannot copy trilist vertices");
                }
                
            }

            
            //dmBuffer::HBuffer buffer2;
            dmVMath::Matrix4 mvp;
            //dmVMath::Matrix4 model;
            //dmVMath::Matrix4 view;
            {
                float slotModelMatrix[16];
                if (openglSlot->isSkinned){
                    createIdentityMatrix(slotModelMatrix);
                } else {
                    convertDBMatrixToGL(slot->globalTransformMatrix, slotModelMatrix);
                }
                float pvMatrix[16], mvpMatrix[16];
                multiplyMatrices(instance->projectionMatrix, viewMatrix, pvMatrix);
                //dmLogInfo("pvMatrix 0,1 : %f, %f", pvMatrix[0], pvMatrix[1]);
                multiplyMatrices(pvMatrix, slotModelMatrix, mvpMatrix);
                //dmLogInfo("mvpMatrix for %s values 0,1,2, 3 : %f, %f, %f, %f", slot_name, mvpMatrix[0], mvpMatrix[4],  mvpMatrix[8], mvpMatrix[12]);

                mvp = {
                    {mvpMatrix[0],   mvpMatrix[1],  mvpMatrix[2],  mvpMatrix[3]},
                    {mvpMatrix[4],   mvpMatrix[5],  mvpMatrix[6],  mvpMatrix[7]},
                    {mvpMatrix[8],   mvpMatrix[9],  mvpMatrix[10], mvpMatrix[11]},
                    {mvpMatrix[12],  mvpMatrix[13], mvpMatrix[14], mvpMatrix[15]},
                };

                /*
                model = {
                    {slotModelMatrix[0],   slotModelMatrix[1],  slotModelMatrix[2],  slotModelMatrix[3]},
                    {slotModelMatrix[4],   slotModelMatrix[5],  slotModelMatrix[6],  slotModelMatrix[7]},
                    {slotModelMatrix[8],   slotModelMatrix[9],  slotModelMatrix[10], slotModelMatrix[11]},
                    {slotModelMatrix[12],  slotModelMatrix[13], slotModelMatrix[14], slotModelMatrix[15]},
                };

                view = {
                    {viewMatrix[0],   viewMatrix[1],  viewMatrix[2],  viewMatrix[3]},
                    {viewMatrix[4],   viewMatrix[5],  viewMatrix[6],  viewMatrix[7]},
                    {viewMatrix[8],   viewMatrix[9],  viewMatrix[10], viewMatrix[11]},
                    {viewMatrix[12],  viewMatrix[13], viewMatrix[14], viewMatrix[15]},
                };*/
               
                
            }    
            
            
            
            //indices NOT USE!!!!
            /*
            dmBuffer::HBuffer buffer3;
            {
                const dmBuffer::StreamDeclaration streams_decl3[] = {
                   {dmHashString64("indices"), dmBuffer::VALUE_TYPE_UINT16, 1},
                };
                
               
                dmBuffer::Result r = dmBuffer::Create(slot_indices.size() / 1, streams_decl3, 1, &buffer3);
            
                if (r == dmBuffer::RESULT_OK) {
                    unsigned short* strm_indices = 0x0;
                
                    uint32_t size = 0;
                    uint32_t components = 0;
                    uint32_t stride = 0;
                    uint32_t count;

                    dmBuffer::Result r1 = dmBuffer::GetStream(buffer3, dmHashString64("indices"), (void**)&strm_indices, &count, &components, &stride);
                
                    if (r1 == dmBuffer::RESULT_OK ) {
                        for(int i = 0; i < count; ++i){
                            for (int c = 0; c < components; ++c){
                                strm_indices[c] = indices[i + c] ;
                            }
                            strm_indices += stride;
                        } 
                    } else {
                         dmLogInfo("Cannot get indices' streams.");
                    }
                    //instance->buffers.Push(luabuf3);
                } else {
                    dmLogInfo("Cannot create indices.");
                    //continue
                }
                
            }   
            */
            

            lua_pushinteger(L, slot_index + 1); 
            // { 1 = {buffer=..., projection=..., indices=...},  2 = {buffer=..., projection=..., indices=...}}

            lua_newtable(L);

            //lua_pushstring(L, "buffer");
            //dmScript::LuaHBuffer luabuf1(buffer1, dmScript::OWNER_LUA);
            //instance->buffers.Push(luabuf1);
            //dmScript::PushBuffer(L, luabuf1);
            //lua_settable(L, -3);

            lua_pushstring(L, "buffer_trilist");
            dmScript::LuaHBuffer luabuf2(buffer_trilist, dmScript::OWNER_LUA);
            //instance->buffers.Push(luabuf2);
            dmScript::PushBuffer(L, luabuf2);
            lua_settable(L, -3);

            lua_pushstring(L, "mtx_mvp");
            dmScript::PushMatrix4(L, mvp);
            lua_settable(L, -3);

            //lua_pushstring(L, "mtx_model");
            //dmScript::PushMatrix4(L, model);
            //lua_settable(L, -3);

            //lua_pushstring(L, "mtx_view");
            //dmScript::PushMatrix4(L, view);
            //lua_settable(L, -3);

            //No use
            //lua_pushstring(L, "indices");
            //dmScript::LuaHBuffer luabuf3(buffer3, dmScript::OWNER_LUA);
            //instance->buffers.Push(luabuf3);
            //dmScript::PushBuffer(L, luabuf3);
            //lua_settable(L, -3);
            
            
            lua_pushstring(L, "name");
            lua_pushstring(L,  slot_name);
            lua_settable(L, -3);

            //lua_pushstring(L,  "buffer_count");
            //lua_pushinteger(L,  slot_vertices.size()/4); //component count 2, and two position and texCoord?
            //lua_settable(L, -3);

            lua_pushstring(L,  "trilist_count");
            lua_pushinteger(L,  trilist.size()/4);
            lua_settable(L, -3);


            
            //lua_pushstring(L,  "indices_count");
            //lua_pushinteger(L,  slot_indices.size() * 1); //component count 1?
            //lua_settable(L, -3);
            

            //table on top
            lua_settable(L, -3); //set table on table

        }

        return 1;
    }


    static int getBatchBuffer(lua_State* L){
        //DM_LUA_STACK_CHECK( L, 1);
        JniBridgeInstance* instance     = (JniBridgeInstance*)lua_touserdata(L, 1);
        //freeBuffers(instance);
        const auto& slots = instance->armature->getSlots();
        

        //projection
        float viewMatrix[16], scaleM[16], transM[16];
        createScaleMatrix(scaleM, instance->worldScale, instance->worldScale, 1.0f);
        createTranslateMatrix(transM, (instance->viewportWidth / 2.0f) + instance->worldTranslateX, (instance->viewportHeight / 2.0f) + instance->worldTranslateY, 0.0f);
        multiplyMatrices(transM, scaleM, viewMatrix);

        //dmLogInfo("View matrix 0,1 : %f, %f", viewMatrix[0], viewMatrix[1])
        auto aabb = instance->armature->getArmatureData()->aabb;
        float x = instance->worldTranslateX -  aabb.x;
        float y = instance->worldTranslateY -  aabb.y;
        float aabb_array[6] = {
           x, y, -1, x + aabb.width, y + aabb.height, 1
        };
        
        //dmLogInfo("AABB: %f, %f, %f, %f", aabb_array[0], aabb_array[1], aabb_array[3], aabb_array[4]);
        
        //instance->buffers.SetCapacity(slots.size() * 2);
        std::vector<float> batch_trilist;
        uint32_t total_len = 0;
        for (int slot_index = 0; slot_index <slots.size(); slot_index++){
            auto& slot  = slots[slot_index];
            auto* openglSlot = static_cast<dragonBones::opengl::OpenGLSlot*>(slot);
            auto slot_indices = openglSlot->indices;
            if (!slot) {
                continue;
            }

            if (!slot->getVisible()) {
                continue;
            }

            if (!slot->getDisplay()) {
                continue;
            }
                    
            if (!openglSlot) {
                continue;
            }

            if (openglSlot->vertices.empty() || openglSlot->indices.empty()) {
                continue;
            }

            total_len += slot_indices.size() * 4;
        }
        batch_trilist.resize(total_len * 20);
        
       
        int offset_trilist = 0;
        for (int slot_index = 0; slot_index <slots.size(); slot_index++){
                auto& slot  = slots[slot_index];
                auto* openglSlot = static_cast<dragonBones::opengl::OpenGLSlot*>(slot);

                if (!slot) {
                    continue;
                }

                if (!slot->getVisible()) {
                    continue;
                }

                if (!slot->getDisplay()) {
                    continue;
                }
                        
                if (!openglSlot) {
                    continue;
                }

                if (openglSlot->vertices.empty() || openglSlot->indices.empty()) {
                    continue;
                }

                const char* slot_name = slot->getName().c_str();
                auto slot_vertices = openglSlot->vertices;
                auto slot_indices = openglSlot->indices;
                //auto indices = slot_indices.data();

                float slotModelMatrix[16];
                if (openglSlot->isSkinned){
                    createIdentityMatrix(slotModelMatrix);
                } else {
                    convertDBMatrixToGL(slot->globalTransformMatrix, slotModelMatrix);
                }
                float pvMatrix[16], mvpMatrix[16];
                multiplyMatrices(instance->projectionMatrix, viewMatrix, pvMatrix);
                //dmLogInfo("pvMatrix 0,1 : %f, %f", pvMatrix[0], pvMatrix[1]);
                multiplyMatrices(pvMatrix, slotModelMatrix, mvpMatrix);
                //dmLogInfo("mvpMatrix for %s values 0,1,2, 3 : %f, %f, %f, %f", slot_name, mvpMatrix[0], mvpMatrix[4],  mvpMatrix[8], mvpMatrix[12]);


                //vertices
                {
                    
                    auto* vertices1 = slot_vertices.data(); 
                    for (int i = 0; i < slot_indices.size() ; i++){
                        unsigned short index = slot_indices[i];
                        int sno = index * 4;
                        float vx = vertices1[sno + 0]; float vy = vertices1[sno + 1]; //positions
                        float tx = vertices1[sno + 2]; float ty = vertices1[sno + 3]; //texture coord

                        batch_trilist[offset_trilist + 0] = vx; 
                        batch_trilist[offset_trilist + 1] = vy; 
                        batch_trilist[offset_trilist + 2] = tx; 
                        batch_trilist[offset_trilist + 3] = ty; 

                        batch_trilist[offset_trilist + 4] = mvpMatrix[0]; 
                        batch_trilist[offset_trilist + 5] = mvpMatrix[1]; 
                        batch_trilist[offset_trilist + 6] = mvpMatrix[2]; 
                        batch_trilist[offset_trilist + 7] = mvpMatrix[3]; 

                        batch_trilist[offset_trilist + 8] = mvpMatrix[4]; 
                        batch_trilist[offset_trilist + 9] = mvpMatrix[5]; 
                        batch_trilist[offset_trilist + 10] = mvpMatrix[6]; 
                        batch_trilist[offset_trilist + 11] = mvpMatrix[7]; 

                        batch_trilist[offset_trilist + 12] = mvpMatrix[8]; 
                        batch_trilist[offset_trilist + 13] = mvpMatrix[9]; 
                        batch_trilist[offset_trilist + 14] = mvpMatrix[10]; 
                        batch_trilist[offset_trilist + 15] = mvpMatrix[11]; 

                        batch_trilist[offset_trilist + 16] = mvpMatrix[12]; 
                        batch_trilist[offset_trilist + 17] = mvpMatrix[13]; 
                        batch_trilist[offset_trilist + 18] = mvpMatrix[14]; 
                        batch_trilist[offset_trilist + 19] = mvpMatrix[15]; 
                         
                        
                        offset_trilist += 20;
                    }

                    
                }
        }

         
        dmBuffer::HBuffer buffer_trilist = 0x0;
        const dmBuffer::StreamDeclaration streams_decl[] = {
            {dmHashString64("a_position"), dmBuffer::VALUE_TYPE_FLOAT32,   3},
            {dmHashString64("a_texCoord"), dmBuffer::VALUE_TYPE_FLOAT32,   2},
            {dmHashString64("a_normal"),   dmBuffer::VALUE_TYPE_FLOAT32,   3},
            {dmHashString64("a_mat"),      dmBuffer::VALUE_TYPE_FLOAT32,  16},
        };
    
        dmBuffer::Result r = dmBuffer::Create(total_len, streams_decl, 4, &buffer_trilist);
    
        if (r == dmBuffer::RESULT_OK) {
            
            dmBuffer::Result rm = dmBuffer::SetMetaData(buffer_trilist, dmHashString32("AABB"), &aabb_array , 6, dmBuffer::VALUE_TYPE_FLOAT32);
            if(rm != dmBuffer::RESULT_OK){
                dmLogInfo("Cannot set AABB");
            }

            float* positions = 0x0;
            float* texCoord = 0x0;
            float* normals = 0x0;
            float* mat = 0x0;

            uint32_t components1 = 0;
            uint32_t components2 = 0;
            uint32_t components3 = 0;
            uint32_t components4 = 0;

            uint32_t stride1 = 0;
            uint32_t stride2 = 0;
            uint32_t stride3 = 0;
            uint32_t stride4 = 0;

            uint32_t count;

            dmBuffer::Result r1 = dmBuffer::GetStream(buffer_trilist, dmHashString64("a_position"), (void**)&positions, &count, &components1,  &stride1);
            dmBuffer::Result r2 = dmBuffer::GetStream(buffer_trilist, dmHashString64("a_texCoord"), (void**)&texCoord,  &count, &components2,  &stride2);
            dmBuffer::Result r3 = dmBuffer::GetStream(buffer_trilist, dmHashString64("a_normal"),   (void**)&normals,   &count, &components3,  &stride3);
            dmBuffer::Result r4 = dmBuffer::GetStream(buffer_trilist, dmHashString64("a_mat"),      (void**)&mat,       &count, &components4,  &stride4);

            //dmLogInfo("Buffer Count, Components: %d, %d", count, components);
            if (r1 == dmBuffer::RESULT_OK && r2 == dmBuffer::RESULT_OK && r4 == dmBuffer::RESULT_OK ) {
                int batch_offset = 0;
                float z = -1;
                for(int i = 0; i < count; ++i){
                    
                    //y axis is up in defold
                    auto pos_x =  batch_trilist[batch_offset + 0];
                    auto pos_y =  batch_trilist[batch_offset + 1];
                    positions[0] =  pos_x ; 
                    positions[1] =  pos_y ; 
                    positions[2] =  z;
                    z += 0.01;
                    
                    //flip y tex coordinates
                    auto tex_x =       batch_trilist[batch_offset + 2];
                    auto tex_y = 1.0 - batch_trilist[batch_offset + 3];
                    texCoord[0]  = tex_x; 
                    texCoord[1]  = tex_y; 

                    normals[0] = 0; 
                    normals[1] = 0; 
                    normals[2] = 1;

                    mat[0] = batch_trilist[batch_offset + 4];
                    mat[1] = batch_trilist[batch_offset + 5];
                    mat[2] = batch_trilist[batch_offset + 6];
                    mat[3] = batch_trilist[batch_offset + 7];
                    mat[4] = batch_trilist[batch_offset + 8];
                    mat[5] = batch_trilist[batch_offset + 9];
                    mat[6] = batch_trilist[batch_offset + 10];
                    mat[7] = batch_trilist[batch_offset + 11];
                    mat[8] = batch_trilist[batch_offset + 12];
                    mat[9] = batch_trilist[batch_offset + 13];
                    mat[10] = batch_trilist[batch_offset + 14];
                    mat[11] = batch_trilist[batch_offset + 15];
                    mat[12] = batch_trilist[batch_offset + 16];
                    mat[13] = batch_trilist[batch_offset + 17];
                    mat[14] = batch_trilist[batch_offset + 18];
                    mat[15] = batch_trilist[batch_offset + 19];

                    positions += stride1;
                    texCoord  += stride2;
                    normals   += stride3;
                    mat       += stride4;

                    batch_offset += 20;
                    //dmLogInfo("vertices for %s values : %f, %f", slot_name, vertices[i]);
                }
            } else {
                dmLogInfo("Cannot get trilist vertices' streams. ");
            }
            
        } else {
            dmLogInfo("Cannot copy trilist vertices");
        }


        lua_newtable(L);

        lua_pushstring(L, "batch_buffer");
        dmScript::LuaHBuffer luabuf2(buffer_trilist, dmScript::OWNER_LUA);
        //instance->buffers.Push(luabuf2);
        dmScript::PushBuffer(L, luabuf2);
        lua_settable(L, -3);
        
        lua_pushstring(L,  "batch_buffer_count");
        lua_pushinteger(L,  total_len);
        lua_settable(L, -3);

        return 1;
    }


    static int debugDraw(lua_State* L){
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        bool debug = luaL_checknumber(L, 2) == 1;

        if (instance && instance->dragonBones){
            instance->dragonBones->debugDraw = debug;
        } else {
            dmLogInfo("Cannot debug. No dragonbones instance.");
        }

        return 1;
        
    }

    static int update(lua_State* L){
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        float dt = (float)luaL_checknumber(L, 2);
        if (instance && instance->dragonBones){
            instance->dragonBones->advanceTime(dt); 
        } else {
            dmLogInfo("Cannot advance time.");
        }

        return 1;
    }

    static int containsPoint(lua_State* L) {
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        if (!instance || !instance->armature) {
            return 0;
        }

        float x = (float)luaL_checknumber(L, 2);
        float y = (float)luaL_checknumber(L, 3);

        // Convert screen coordinates to armature space coordinates
        const float armatureX = (x - (instance->viewportWidth / 2.0f) - instance->worldTranslateX) / instance->worldScale;
        const float armatureY = (y - (instance->viewportHeight / 2.0f) - instance->worldTranslateY) / instance->worldScale;

        auto* slot = instance->armature->containsPoint(armatureX, armatureY);
        if (slot) {
            lua_pushstring(L, slot->getName().c_str());
        } else {
            lua_pushnil(L);
        }

        return 1;
    }

    static int setWorldScale(lua_State* L) {
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        float scale = (float)luaL_checknumber(L, 2);
        if (instance) {
            instance->worldScale = scale;
        }
        return 1;
    }

    static int setWorldTranslation(lua_State* L) {
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        float x = (float)luaL_checknumber(L, 2);
        float y = (float)luaL_checknumber(L, 3);
        if (instance) {
            instance->worldTranslateX = x;
            instance->worldTranslateY = y;
        }
        return 1;
    }

    static int setBonePosition(lua_State* L) {
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        const char* boneNameChars = luaL_checkstring(L, 2);
        float x = (float)luaL_checknumber(L, 3);
        float y = (float)luaL_checknumber(L, 4);

        std::string name(boneNameChars);
        auto* bone = instance->armature->getBone(name);
        if (bone) {
            // Convert screen coordinates to armature space
            //const float armatureX = (x - (instance->viewportWidth / 2.0f) - instance->worldTranslateX) / instance->worldScale;
            //const float armatureY = (y - (instance->viewportHeight / 2.0f) - instance->worldTranslateY) / instance->worldScale;

            bone->offsetMode = dragonBones::OffsetMode::Additive;
            bone->offset.x = x;
            bone->offset.y = y;
            bone->invalidUpdate();
        }
        return 1;
    }

    static int setBoneRotation(lua_State* L) {
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        const char* boneNameChars = luaL_checkstring(L, 2);
        float angle = (float)luaL_checknumber(L, 3);
      

        std::string name(boneNameChars);
        auto* bone = instance->armature->getBone(name);
        if (bone) {
            // Convert screen coordinates to armature space
           
            bone->offsetMode = dragonBones::OffsetMode::Additive;
            bone->offset.rotation = angle;
            bone->invalidUpdate();
        }
        return 1;
    }

    static int setSlotVisibility(lua_State* L) {
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        const char* boneNameChars = luaL_checkstring(L, 2);
        bool val = (bool)lua_toboolean(L, 3);
       
        std::string name(boneNameChars);

        auto* slot = instance->armature->getSlot(name);
        if (slot) {
            slot->setVisible(val);
            slot->invalidUpdate();
        }
        return 1;
    }

    static int setSlotDisplayIndex(lua_State* L) {
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        const char* boneNameChars = luaL_checkstring(L, 2);
        int index = luaL_checkint(L, 3);
       
        std::string name(boneNameChars);

        auto* slot = instance->armature->getSlot(name);
        if (slot) {
            slot->setDisplayIndex(index);
            slot->invalidUpdate();
        }
        return 1;
    }

    //TODO
    //instead of building one armature, build all the define armatures
    //keep a list of build armatures, their cache and skin name
    //instance->factory->buildArmature(armatureNameToBuild, "CACHE_NAME", "SKIN_NAME", dragonBonesData->name);
    static int replaceSkin(lua_State* L){
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        if(!(instance && instance->armature)) {
            return 0;
        }

        const char* _name = luaL_checkstring(L, 2);
        std::string skin_name(_name);

        //const auto partArmatureData = instance->factory->getArmatureData(skin_name, cache_name);
        // Replace skin.
        //if(partArmatureData) {
        //   instance->factory->replaceSkin(instance->armature, partArmatureData->defaultSkin);
        //}
        dmLogInfo("Todo: Skin replacement.")

        return 1;
    }


    static int loadSkinData(lua_State* L){
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        if(!(instance && instance->armature)) {
            return 0;
        }

        const char* _name = luaL_checkstring(L, 2);
        std::string armature_name(_name);

        //auto armature2 =  instance->factory->buildArmature(armature_name, armature_name + "_CACHE" , armature_name + "_SKIN");
        //_tryBuildArmature(instance, armature_name)

        
        dmLogInfo("Todo: Load skin data.")

        return 1;
    }




    static int setFlipX(lua_State* L) {
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        if(!(instance && instance->armature)) {
            return 0;
        }
        bool val = (bool)lua_toboolean(L, 2);
       
        instance->armature->setFlipX(val);

        return 1;
    }

    static int setFlipY(lua_State* L) {
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        if(!(instance && instance->armature)) {
            return 0;
        }
        bool val = (bool)lua_toboolean(L, 2);
       
        instance->armature->setFlipY(val);

        return 1;
    }

    static int resetBone(lua_State* L) {
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        const char* boneNameChars = luaL_checkstring(L, 2);

        std::string name(boneNameChars);
        auto* bone = instance->armature->getBone(name);
        if (bone) {
            #undef None
            bone->offsetMode = dragonBones::OffsetMode::None;
            bone->invalidUpdate();
            #define None                 0L /* universal null resource or null atom */
            //in case stuff break;
        }
        return 1;
    }

    static int stopAnimation(lua_State* L) {
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        const char* nameChars = luaL_checkstring(L, 2);

        if (!instance || !instance->armature || !instance->armature->getAnimation()) {
            return 0;
        }

        std::string name(nameChars);

        dmLogInfo("Stopping animation: '%s'.", name.c_str());
        instance->armature->getAnimation()->stop(name);
        return 1;
    }
    
    static int getAnimationNames(lua_State* L) {
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        if (!instance || !instance->armature) {
            return 0;
        }

        const auto& animationNames = instance->armature->getAnimation()->getAnimationNames();
        if (animationNames.empty()) {
            return 0;
        }

        lua_newtable(L);

        for (size_t i = 0; i < animationNames.size(); ++i) {
            const char * anim_name = animationNames[i].c_str();
            lua_pushinteger(L, i + 1);
            lua_pushstring(L,  anim_name);
            lua_settable(L, -3);

        }

        return 1;
    }

    static int fadeInAnimation(lua_State* L) {
        //jstring animation_name, jint layer, jint loop, jfloat fade_in_time

        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        if (!instance || !instance->armature) {
            dmLogInfo("FadeIn: No instance or armature.");
            return 1;
        }

        const char* nameChars = luaL_checkstring(L, 2);
        std::string name(nameChars);

        // playTimes: -1 means use default value, 0 means loop infinitely, [1~N] means repeat N times.
        // The `loop` parameter from Kotlin is: 0 for infinite, 1 for once, etc. This maps directly to playTimes.
        int layer          = (int)luaL_checknumber(L, 3);
        int playTimes      = (int)luaL_checknumber(L, 4);
        float fade_in_time = (float)luaL_checknumber(L, 5);
        

        // Use fadeIn to enable blending and layering.
        // Set fadeOutMode to None to prevent animations on different layers from stopping each other.
        // Use SameLayerAndGroup if you want an animation to replace another on the same layer.
        if(instance->armature->getAnimation()->fadeIn(name, (float)fade_in_time, playTimes, (int)layer, "", dragonBones::AnimationFadeOutMode::SameLayerAndGroup)) {
            dmLogInfo("Fading in animation: '%s' on layer %d, loop: %d, fade: %f", name.c_str(), (int)layer, playTimes, (float)fade_in_time);
        } else {
            dmLogInfo("Animation not found: '%s'", name.c_str());
        }
        return 1;
    }

    static const luaL_reg DRAGONBONES_COMP_FUNCTIONS[] =
    {
           
            {"create",               init           },
            {"update",               update         },
            {"resize",               resize         },
            {"destroy",              destroy        },
            {"load_data",            loadData2      },
            {"get_no_slots",         getNoSlots     },
            {"get_buffers",          getBuffers     },
            {"free_buffers",         freeBuffers    },
            {"fade_in_animation",       fadeInAnimation      },
            {"get_anination_names",     getAnimationNames    },
            {"contains_point",          containsPoint        },
            {"set_world_scale",         setWorldScale        },
            {"scale",                   setWorldScale        },
            {"set_world_translation",   setWorldTranslation  },
            {"move",                    setWorldTranslation  },
            {"set_bone_position",       setBonePosition      },
            {"set_bone_rotation",       setBoneRotation      },
            {"reset_bone",              resetBone            },
            {"stop_animation",          stopAnimation        },
            {"debug_draw",              debugDraw            },
            {"get_batch_buffer",        getBatchBuffer       },
            {"set_slot_visibility",     setSlotVisibility    },
            {"set_flip_x",              setFlipX             },
            {"set_flip_y",              setFlipY             },
            {"add_event_callback",      addEventListener     },
            {"remove_event_callback",   removeEventListener  },
            //{"replace_skin",            replaceSkin          },
            {0, 0}
    };

    void ScriptInit(lua_State* L)
    {
        luaL_register(L, "dragonbones", DRAGONBONES_COMP_FUNCTIONS);

        //lua_pushinteger(L, dmGameSystemDDF::MixBlend::MIX_BLEND_ADD);
        //lua_setfield(L, -2, "MIX_BLEND_ADD");

        //lua_pop(L, 1);

    }

    void ScriptUpdate(lua_State* L)
    {
       
        //update world
    }
}