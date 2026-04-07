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

 
namespace dmDragonBones
{

    struct JniBridgeInstance {
        dmGameObject::HComponent m_component = nullptr;


        dragonBones::DragonBones* dragonBones = nullptr;
        dragonBones::Armature* armature = nullptr;
        dragonBones::opengl::OpenGLFactory* factory = nullptr;
        
        // OpenGL ES 2.0 rendering variables
        /*GLuint programId = 0; 
        GLint positionLocation = -1;
        GLint texCoordLocation = -1;
        GLint mvpMatrixLocation = -1;
        GLint textureLocation = -1; */

        //GLfloat
        float projectionMatrix[16];
    
        float viewportWidth  = 2048.0f;
        float viewportHeight = 2048.0f;

       /*
           instance->positionLocation = glGetAttribLocation(instance->programId, "a_position");
            instance->texCoordLocation = glGetAttribLocation(instance->programId, "a_texCoord");
            instance->mvpMatrixLocation = glGetUniformLocation(instance->programId, "u_mvpMatrix");
            instance->textureLocation = glGetUniformLocation(instance->programId, "u_texture");
       */

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

        ~JniBridgeInstance() {
            if (dragonBones) {
                delete dragonBones;
                dragonBones = nullptr;
            }
            if (factory) {
                delete factory;
                factory = nullptr;
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
        
        matrix[8] = 0.0f;
        matrix[9] = 0.0f;
        matrix[10] = -2.0f / (_far - _near);
        matrix[11] = 0.0f;
        
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

        dmLogInfo("Creating armature on GL thread...");
        
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
            dmLogInfo("Armature building done.");
        } else {
            dmLogError("Failed to build armature '%s'.", armatureNameToBuild.c_str());
        }
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
        createOrthographicMatrix(0.0f, instance->viewportWidth, 0.0f, instance->viewportHeight, -1.0f, 1.0f, instance->projectionMatrix);

        dmLogInfo("projectionMatrix 0, 1: %f, %f", instance->projectionMatrix[0], instance->projectionMatrix[1]);
        dmLogInfo("DragonBones Initialized");

        /*
        dmGraphics::HContext context = dmGraphics::GetInstalledContext();
        instance->m_GraphicsContext = context ; // ->m_Contexts.Get(dmHashString64("graphics"));
        //instance->m_RenderContext   = context  ->m_Contexts.Get(dmHashString64("render"));

        dmGraphics::HVertexStreamDeclaration stream_declaration = dmGraphics::NewVertexStreamDeclaration(instance->m_GraphicsContext);
        dmGraphics::AddVertexStream(stream_declaration, "u_mvpMatrix", 4, dmGraphics::TYPE_FLOAT, true);
        dmGraphics::AddVertexStream(stream_declaration, "a_position", 2, dmGraphics::TYPE_FLOAT, false);
        dmGraphics::AddVertexStream(stream_declaration, "a_texCoord", 2, dmGraphics::TYPE_FLOAT, true);

        //provided by material
        //dmGraphics::AddVertexStream(stream_declaration, "u_texture", 1, dmGraphics::TYPE_FLOAT, false);

        instance->m_VertexDeclaration = dmGraphics::NewVertexDeclaration(instance->m_GraphicsContext, stream_declaration, 4 * sizeof(float));
        instance->m_VertexBuffer = dmGraphics::NewVertexBuffer(instance->m_GraphicsContext, 0, 0x0, dmGraphics::BUFFER_USAGE_DYNAMIC_DRAW);

        dmGraphics::DeleteVertexStreamDeclaration(stream_declaration);

        dmGraphics::HIndexBuffer indexBuffer = dmGraphics::NewIndexBuffer(instance->m_GraphicsContext, 0, 0x0, dmGraphics::BUFFER_USAGE_DYNAMIC_DRAW);
        instance->m_IndexBuffer = indexBuffer;

        instance->m_component = component;
        instance->m_RenderObjects.SetCapacity(4); //minimum slots
        */

        lua_pushlightuserdata(L, instance);
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
      2,3. are skeleton json, texture atlas json loaded with data1 = resource.load("/path")
      make sure to set texture in mesh component
      '''
        instance = dragonbones.init()
        skeleton = resource.load('/path')
        ...
        dragonbones.loadData(instance, skeleton, atlas)
        dragonbones.update()

        --dragonbones.get_buffers()
        --for each buffer
          --go.set("#mesh", "vertices", )
          --go.set("#mesh", "vertices", )
      '''
    */
    static int loadData2(lua_State* L) {
        //char* skeleton_data, size_t skeleton_len, char* texture_json_data, int texture_json_len, char* texture_png_data, int texture_png_len
        
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);

        
        dmBuffer::HBuffer skeleton_data_buffer =  dmScript::CheckBufferUnpack(L, 2);
        uint8_t* skeleton_bytes = 0x0;
        uint32_t skeleton_len   = 0;
        dmBuffer::GetBytes(skeleton_data_buffer, (void**)&skeleton_bytes, &skeleton_len);
        dmLogInfo("Skeleton no of bytes: %d", skeleton_len);

        dmBuffer::HBuffer texture_json_buffer =  dmScript::CheckBufferUnpack(L, 3);
        uint8_t* texture_json_bytes = 0x0;
        uint32_t texture_json_len   = 0;
        dmBuffer::GetBytes(texture_json_buffer, (void**)&texture_json_bytes, &texture_json_len);
        dmLogInfo("Atlas json no of bytes: %d", texture_json_len);

        
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

        dmLogInfo("Buffering data from byte arrays...");

        
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
            dmLogInfo("Data successfully buffered.");
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
       
    }

   /*
    static void FillRenderObject(JniBridgeInstance*  world,
        dmRender::HRenderContext                   render_context,
        dmRender::RenderObject&                    ro,
        dmGameSystem::HComponentRenderConstants    constants,
        dmGraphics::HTexture                       texture,
        dmRender::HMaterial                        material,
        uint32_t                                   vertex_start,
        uint32_t                                   vertex_count)
    {
        ro.Init();
        ro.m_VertexDeclaration = world->m_VertexDeclaration;
        ro.m_VertexBuffer      = world->m_VertexBuffer;
        ro.m_PrimitiveType     = dmGraphics::PRIMITIVE_TRIANGLES;
        ro.m_VertexStart       = vertex_start;
        ro.m_VertexCount       = vertex_count;
        ro.m_Textures[0]       = texture;
        ro.m_Material          = material;

        if (constants)
        {
            dmGameSystem::EnableRenderObjectConstants(&ro, constants);
        }

        ro.m_SetBlendFactors = 1;

        ro.m_SourceBlendFactor = dmGraphics::BLEND_FACTOR_ONE;
        ro.m_DestinationBlendFactor = dmGraphics::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        //ro.m_SetStencilTest =
        
           
        dmRender::AddToRender(render_context, &ro);
    }
    */

    //creating the meshes
    static int getNoSlots(lua_State* L){
        JniBridgeInstance* instance     = (JniBridgeInstance*)lua_touserdata(L, 1);
        const auto& slots = instance->armature->getSlots();
        lua_pushinteger(L, slots.size()); 
        return 1;
    }

    static int freeBuffers(lua_State* L){
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        //clear save buffers
        for(int i = 0 ; i < instance->buffers.Size(); i++){
            dmBuffer::Destroy(instance->buffers[i].m_Buffer);
        }
        return 1;
    }
    
    //return table 
    static int getBuffers(lua_State* L){
        //DM_LUA_STACK_CHECK( L, 1);
        JniBridgeInstance* instance     = (JniBridgeInstance*)lua_touserdata(L, 1);
        //freeBuffers(instance);
        const auto& slots = instance->armature->getSlots();
        auto aabb = instance->armature->getArmatureData()->aabb;
        float aabb_array[6] = {aabb.x, aabb.y, -1, aabb.x + aabb.width, aabb.y + aabb.height, 1};

        //projection
        float viewMatrix[16], scaleM[16], transM[16];
        createScaleMatrix(scaleM, instance->worldScale, instance->worldScale, 1.0f);
        createTranslateMatrix(transM, (instance->viewportWidth / 2.0f) + instance->worldTranslateX, (instance->viewportHeight / 2.0f) + instance->worldTranslateY, 0.0f);
        multiplyMatrices(transM, scaleM, viewMatrix);

        //dmLogInfo("View matrix 0,1 : %f, %f", viewMatrix[0], viewMatrix[1])
        
        lua_newtable(L);
        
        instance->buffers.SetCapacity(slots.size() * 2);
       

        for (int slot_index = 0; slot_index <slots.size(); slot_index++){
            auto& slot  = slots[slot_index];
            auto* openglSlot = static_cast<dragonBones::opengl::OpenGLSlot*>(slot);
            const char* slot_name = slot->getName().c_str();
            auto slot_vertices = openglSlot->vertices;
            auto slot_indices = openglSlot->indices;
            auto indices = slot_indices.data();

            //vertices
            dmBuffer::HBuffer buffer1 = 0x0;
            {
                
                auto* vertices = slot_vertices.data();
                const dmBuffer::StreamDeclaration streams_decl[] = {
                    {dmHashString64("a_position"), dmBuffer::VALUE_TYPE_FLOAT32, 2},
                    {dmHashString64("a_texCoord"), dmBuffer::VALUE_TYPE_FLOAT32, 2},
                    {dmHashString64("a_normal"),   dmBuffer::VALUE_TYPE_FLOAT32, 3},
                };
            
                dmBuffer::Result r = dmBuffer::Create(slot_vertices.size()/4, streams_decl, 3, &buffer1);
            
                if (r == dmBuffer::RESULT_OK) {
                   
                    dmBuffer::Result rm = dmBuffer::SetMetaData(buffer1, dmHashString32("AABB"), &aabb_array , 6, dmBuffer::VALUE_TYPE_FLOAT32);
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

                    dmLogInfo("Buffer Count, Components: %d, %d", count, components);
                    if (r1 == dmBuffer::RESULT_OK && r2 == dmBuffer::RESULT_OK) {
                        int offset = 0;
                        for(int i = 0; i < count; ++i){
                            for (int c = 0; c < components; ++c) {
                                auto pos = vertices[offset + c];
                                auto tex = vertices[offset + c + 2] ;

                                positions[c] = c == 0 ? pos : -pos ; //y axis is up in defold
                                texCoord[c]  = c == 0 ? tex : 1.0 - tex ; //flip tex coordinates
                            }

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
                    */
                } else {
                    dmLogInfo("Cannot copy vertices");
                }
                
            }

            
            //dmBuffer::HBuffer buffer2;
            dmVMath::Matrix4 projection;
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
                dmLogInfo("mvpMatrix for %s values 0,1,2, 3 : %f, %f, %f, %f", slot_name, mvpMatrix[0], mvpMatrix[1],  mvpMatrix[2], mvpMatrix[3]);

                /*projection = {
                    {mvpMatrix[0], mvpMatrix[1],mvpMatrix[2],mvpMatrix[3]},
                    {mvpMatrix[4], mvpMatrix[5],mvpMatrix[6],mvpMatrix[7]},
                    {mvpMatrix[8], mvpMatrix[9],mvpMatrix[10],mvpMatrix[11]},
                    {mvpMatrix[12], mvpMatrix[13],mvpMatrix[14],mvpMatrix[15]},
                };*/
                projection = {
                    {mvpMatrix[0], mvpMatrix[4],mvpMatrix[8],mvpMatrix[12]},
                    {mvpMatrix[1], mvpMatrix[5],mvpMatrix[9],mvpMatrix[13]},
                    {mvpMatrix[2], mvpMatrix[6],mvpMatrix[10],mvpMatrix[14]},
                    {mvpMatrix[3], mvpMatrix[7],mvpMatrix[11],mvpMatrix[15]},
                };
                
            }    
            
            
            
            //indices
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
            

            lua_pushinteger(L, slot_index + 1); 
            // { 1 = {buffer=..., projection=..., indices=...},  2 = {buffer=..., projection=..., indices=...}}

            lua_newtable(L);

            lua_pushstring(L, "buffer");
            dmScript::LuaHBuffer luabuf1(buffer1, dmScript::OWNER_C);
            instance->buffers.Push(luabuf1);
            dmScript::PushBuffer(L, luabuf1);
            lua_settable(L, -3);

            lua_pushstring(L, "projection");
            dmScript::PushMatrix4(L, projection);
            lua_settable(L, -3);

            lua_pushstring(L, "indices");
            dmScript::LuaHBuffer luabuf3(buffer3, dmScript::OWNER_C);
            instance->buffers.Push(luabuf3);
            dmScript::PushBuffer(L, luabuf3);
            lua_settable(L, -3);
            
            lua_pushstring(L, "name");
            lua_pushstring(L,  slot_name);
            lua_settable(L, -3);

            lua_pushstring(L,  "buffer_count");
            lua_pushinteger(L,  slot_vertices.size()/4); //component count 2, and two position and texCoord?
            lua_settable(L, -3);

            lua_pushstring(L,  "indices_count");
            lua_pushinteger(L,  slot_indices.size() * 1); //component count 1?
            lua_settable(L, -3);

            //table on top
            lua_settable(L, -3); //set table on table

            break;
        }

        return 1;
    }

    static int update(lua_State* L){
        JniBridgeInstance* instance     =  (JniBridgeInstance*)lua_touserdata(L, 1);
        if (instance && instance->dragonBones){
            instance->dragonBones->advanceTime(1.0f / 60.0f); //!!!!!!!!!!!!!!!!!!!!!!!!
            //IMPORTANT IMPORTANT
            
            /*
            if (instance->armature) {
                // 1. Clear the screen
                //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); //see rendered object below

               

                // 2. Setup the rendering program and global GL state
                //glUseProgram(instance->programId);
                //glEnableVertexAttribArray(instance->positionLocation);
                //glEnableVertexAttribArray(instance->texCoordLocation);
                
                //glActiveTexture(GL_TEXTURE0);
                //glUniform1i(instance->textureLocation, 0);
                
                // 3. Create a "view" matrix to scale and center the entire armature
                float viewMatrix[16], scaleM[16], transM[16];
                createScaleMatrix(scaleM, instance->worldScale, instance->worldScale, 1.0f);
                createTranslateMatrix(transM, (instance->viewportWidth / 2.0f) + instance->worldTranslateX, (instance->viewportHeight / 2.0f) + instance->worldTranslateY, 0.0f);
                multiplyMatrices(transM, scaleM, viewMatrix);
                
                // 4. Render each slot
                const auto& slots = instance->armature->getSlots();
                instance->m_RenderObjects.SetCapacity(slots.size());

                int renderedSlots = 0;
                for (int slot_index = 0; slot_index < slots.size(); slot_index++) {
                    const auto& slot  = slots[slot_index];
                    dmRender::RenderObject& ro = instance->m_RenderObjects[slot_index];
                    
                    if (!slot) {
                        dmLogDebug("onDrawFrame: Skipping null slot.");
                        continue;
                    }

                    if (!slot->getVisible()) {
                        dmLogDebug("onDrawFrame: Slot '%s' is not visible.", slot->getName().c_str());
                        continue;
                    }

                    if (!slot->getDisplay()) {
                        // LOGW("onDrawFrame: Slot '%s' has no display object.", slot->getName().c_str());
                        continue;
                    }
                    
                    auto* openglSlot = static_cast<dragonBones::opengl::OpenGLSlot*>(slot);
                    if (!openglSlot) {
                        dmLogDebug("onDrawFrame: Slot '%s' could not be cast to OpenGLSlot.", slot->getName().c_str());
                        continue;
                    }

                    if (openglSlot->vertices.empty() || openglSlot->indices.empty() || openglSlot->textureID == 0) {
                        dmLogDebug("onDrawFrame: Skipping slot '%s' due to empty buffers or texture ID 0 (vertices: %zu, indices: %zu, textureID: %u)",
                            slot->getName().c_str(), openglSlot->vertices.size(), openglSlot->indices.size(), openglSlot->textureID);
                        continue;
                    }

                    // A. Get this slot's unique transformation matrix
                    float slotModelMatrix[16];
                    if (openglSlot->isSkinned)
                    {
                        createIdentityMatrix(slotModelMatrix);
                    }
                    else
                    {
                        convertDBMatrixToGL(slot->globalTransformMatrix, slotModelMatrix);
                    }

                    // B. Create the final MVP matrix: MVP = Projection * View * SlotModel
                    float pvMatrix[16], mvpMatrix[16];
                    multiplyMatrices(instance->projectionMatrix, viewMatrix, pvMatrix);
                    multiplyMatrices(pvMatrix, slotModelMatrix, mvpMatrix);
                    
                    // C. Pass the final matrix to the shader
                    //glUniformMatrix4fv(, 1, GL_FALSE, mvpMatrix);
                    dmGraphics::SetVertexBufferData(instance->m_VertexBuffer, 4,  
                        mvpMatrix, dmGraphics::BufferUsage::BUFFER_USAGE_DYNAMIC_DRAW );
                    
                    // D. Bind the texture and draw :: done by material
                    //glBindTexture(GL_TEXTURE_2D, openglSlot->textureID);
                    
                    //glVertexAttribPointer(instance->positionLocation, 2, GL_FLOAT, GL_FALSE, stride, openglSlot->vertices.data());
                    //glVertexAttribPointer(instance->texCoordLocation, 2, GL_FLOAT, GL_FALSE, stride, (const GLvoid*)(openglSlot->vertices.data() + 2));
                    
                    //const GLsizei stride = 4 * sizeof(float); set in stream
                    dmGraphics::SetVertexBufferData(instance->m_VertexBuffer, openglSlot->vertices.size(),  
                        openglSlot->vertices.data(), dmGraphics::BufferUsage::BUFFER_USAGE_DYNAMIC_DRAW );
                    dmGraphics::SetVertexBufferData(instance->m_VertexBuffer, openglSlot->vertices.size(),  
                        (const void*)(openglSlot->vertices.data() + 2), dmGraphics::BufferUsage::BUFFER_USAGE_DYNAMIC_DRAW );
                    
                    //see struct RenderObject in render.h
                    /
                    
                    //Render objects represent an actual draw call
                    //@struct
                    //@name RenderObject
    

                    //          dmGraphics::HTexture texture = GetSpineScene(first)->m_TextureSet->m_Texture->m_Texture; // spine - texture set resource - texture resource - texture
                    //                   get from dragonbones loaded textureID ??? or from model component???
                    //          dmRender::HMaterial material = GetMaterial(first);

                    // vertex buffer
                    //         HVertexBuffer NewVertexBuffer(HContext context, uint32_t size, const void* data, BufferUsage buffer_usage);
                    //vertex declaration
                    //         HVertexDeclaration NewVertexDeclaration(HContext context, HVertexStreamDeclaration stream_declaration, uint32_t stride);
                    //         void AddVertexStream(HVertexStreamDeclaration stream_declaration, const char* name, uint32_t size, Type type, bool normalize);

                    // Result AddToRender(HRenderContext context, RenderObject* ro);
                    

                    //instance->positionLocation === 
                    // instance->positionLocation = glGetAttribLocation(instance->programId, "a_position");
                    // instance->texCoordLocation = glGetAttribLocation(instance->programId, "a_texCoord");
                    // instance->mvpMatrixLocation = glGetUniformLocation(instance->programId, "u_mvpMatrix");
                    // instance->textureLocation = glGetUniformLocation(instance->programId, "u_texture");
                    

                    //a_position, a_texCoord etc will be AddVertexStream :: const char* name ????

                    
                    
                    //will be m_IndexBuffer??
                    //HIndexBuffer NewIndexBuffer(HContext context, uint32_t size, const void* data, BufferUsage buffer_usage);
                    dmGraphics::SetIndexBufferData(instance->m_IndexBuffer, openglSlot->indices.size(),openglSlot->indices.data(), 
                        dmGraphics::BufferUsage::BUFFER_USAGE_DYNAMIC_DRAW );
                    
                    //glDrawElements(GL_TRIANGLES, openglSlot->indices.size(), GL_UNSIGNED_SHORT, openglSlot->indices.data());
                    
                    // 
                    //FillRenderObject(instance, render_context, ro, NULL, texture, material, 0, openglSlot->vertices.size());
                    //render script ?
                    renderedSlots++;
                }
                
                if (renderedSlots == 0 && !slots.empty()) {
                    dmLogDebug("onDrawFrame: Rendered 0 slots out of %zu.", slots.size());
                }
                
                // 5. Cleanup
                //glDisableVertexAttribArray(instance->positionLocation);
                //glDisableVertexAttribArray(instance->texCoordLocation);
            }
            
            */
        }

        return 1;
    }

    /*
    JNIEXPORT jobjectArray JNICALL
    Java_com_dragonbones_JniBridge_getAnimationNames(JNIEnv *env, jclass clazz) {
        auto* instance = getInstance();
        if (!instance || !instance->armature) {
            return nullptr;
        }

        const auto& animationNames = instance->armature->getAnimation()->getAnimationNames();
        if (animationNames.empty()) {
            return nullptr;
        }

        jclass stringClass = env->FindClass("java/lang/String");
        jobjectArray stringArray = env->NewObjectArray(animationNames.size(), stringClass, nullptr);

        for (size_t i = 0; i < animationNames.size(); ++i) {
            jstring javaString = env->NewStringUTF(animationNames[i].c_str());
            env->SetObjectArrayElement(stringArray, i, javaString);
            env->DeleteLocalRef(javaString);
        }

        return stringArray;
    }

    JNIEXPORT void JNICALL
    Java_com_dragonbones_JniBridge_fadeInAnimation(JNIEnv *env, jclass clazz, jstring animation_name, jint layer, jint loop, jfloat fade_in_time) {
        auto* instance = getInstance();
        if (!instance || !instance->armature) {
            return;
        }

        const char* nameChars = env->GetStringUTFChars(animation_name, nullptr);
        std::string name(nameChars);
        env->ReleaseStringUTFChars(animation_name, nameChars);

        // playTimes: -1 means use default value, 0 means loop infinitely, [1~N] means repeat N times.
        // The `loop` parameter from Kotlin is: 0 for infinite, 1 for once, etc. This maps directly to playTimes.
        int playTimes = (int)loop;

        // Use fadeIn to enable blending and layering.
        // Set fadeOutMode to None to prevent animations on different layers from stopping each other.
        // Use SameLayerAndGroup if you want an animation to replace another on the same layer.
        if(instance->armature->getAnimation()->fadeIn(name, (float)fade_in_time, playTimes, (int)layer, "", dragonBones::AnimationFadeOutMode::SameLayerAndGroup)) {
            LOGI("Fading in animation: '%s' on layer %d, loop: %d, fade: %f", name.c_str(), (int)layer, playTimes, (float)fade_in_time);
        } else {
            LOGW("Animation not found: '%s'", name.c_str());
        }
    }

    JNIEXPORT jstring JNICALL
    Java_com_dragonbones_JniBridge_containsPoint(JNIEnv *env, jclass clazz, jfloat x, jfloat y) {
        auto* instance = getInstance();
        if (!instance || !instance->armature) {
            return nullptr;
        }

        // Convert screen coordinates to armature space coordinates
        const float armatureX = (x - (viewportWidth / 2.0f) - instance->worldTranslateX) / instance->worldScale;
        const float armatureY = (y - (viewportHeight / 2.0f) - instance->worldTranslateY) / instance->worldScale;

        auto* slot = instance->armature->containsPoint(armatureX, armatureY);
        if (slot) {
            return env->NewStringUTF(slot->getName().c_str());
        }

        return nullptr;
    }

    JNIEXPORT void JNICALL
    Java_com_dragonbones_JniBridge_setWorldScale(JNIEnv *env, jclass clazz, jfloat scale) {
        auto* instance = getInstance();
        if (instance) {
            instance->worldScale = scale;
        }
    }

    JNIEXPORT void JNICALL
    Java_com_dragonbones_JniBridge_setWorldTranslation(JNIEnv *env, jclass clazz, jfloat x, jfloat y) {
        auto* instance = getInstance();
        if (instance) {
            instance->worldTranslateX = x;
            instance->worldTranslateY = y;
        }
    }

    JNIEXPORT void JNICALL
    Java_com_dragonbones_JniBridge_overrideBonePosition(JNIEnv *env, jclass clazz, jstring bone_name, jfloat x, jfloat y) {
        auto* instance = getInstance();
        if (!instance || !instance->armature) return;

        const char* boneNameChars = env->GetStringUTFChars(bone_name, nullptr);
        auto* bone = instance->armature->getBone(boneNameChars);
        env->ReleaseStringUTFChars(bone_name, boneNameChars);

        if (bone) {
            // Convert screen coordinates to armature space
            const float armatureX = (x - (viewportWidth / 2.0f) - instance->worldTranslateX) / instance->worldScale;
            const float armatureY = (y - (viewportHeight / 2.0f) - instance->worldTranslateY) / instance->worldScale;

            bone->offsetMode = dragonBones::OffsetMode::Override;
            bone->offset.x = armatureX;
            bone->offset.y = armatureY;
            bone->invalidUpdate();
        }
    }

    JNIEXPORT void JNICALL
    Java_com_dragonbones_JniBridge_resetBone(JNIEnv *env, jclass clazz, jstring bone_name) {
        auto* instance = getInstance();
        if (!instance || !instance->armature) return;

        const char* boneNameChars = env->GetStringUTFChars(bone_name, nullptr);
        auto* bone = instance->armature->getBone(boneNameChars);
        env->ReleaseStringUTFChars(bone_name, boneNameChars);

        if (bone) {
            bone->offsetMode = dragonBones::OffsetMode::None;
            bone->invalidUpdate();
        }
    }

    JNIEXPORT void JNICALL
    Java_com_dragonbones_JniBridge_stopAnimation(JNIEnv *env, jclass clazz, jstring animation_name) {
        auto* instance = getInstance();
        if (!instance || !instance->armature || !instance->armature->getAnimation()) {
            return;
        }

        const char* nameChars = env->GetStringUTFChars(animation_name, nullptr);
        std::string name(nameChars);
        env->ReleaseStringUTFChars(animation_name, nameChars);

        LOGI("Stopping animation: '%s' via JNI call.", name.c_str());
        instance->armature->getAnimation()->stop(name);
    }
    */


    static const luaL_reg DRAGONBONES_COMP_FUNCTIONS[] =
    {
           
            {"create",          init           },
            {"update",          update         },
            {"resize",          resize         },
            {"destroy",         destroy        },
            {"load_data",       loadData2      },
            {"get_no_slots",    getNoSlots     },
            {"get_buffers",     getBuffers     },
            {"free_buffers",    freeBuffers    },
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