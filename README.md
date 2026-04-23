**USAGE**
- The DragonBones Editor can be obtained from [Dragon Bones Editor](https://web.archive.org/web/20211013162034/http://tool.egret-labs.org/DragonBonesPro/DragonBonesPro-v5.6.3.exe)

- In Defold create a project and add custom resource folder hereby called custom_res/ and update game.project' custom resource entry.

- In DragonBones Editor export your animations to custom_res/ folder. Export texture in powers of 2.  Make sure three files are exported to custom resource folder. Two jsons and one png.  

- Add camera to your collection. Set camera properties such as zoom, orthographic etc.

- Add render script. Choose dragonbones.render_script in game.project or add the following to your render script. After the model rendering.
    ```
        -- in init, add dragon predicate
        self.predicates = create_predicates("tile", "gui", "particle", "model", "debug_text", "dragon")

        --in update, after model rendering
        -- render `dragon`
        --
        render.enable_state(graphics.STATE_BLEND)
        render.set_blend_func(graphics.BLEND_FACTOR_ONE, graphics.BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
        render.draw(predicates.dragon, draw_options_world)
        render.set_blend_func(graphics.BLEND_FACTOR_SRC_ALPHA, graphics.BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
        render.disable_state(graphics.STATE_BLEND)
    ```

- Add BatchDragonModel.go to your collection. Rename it if needed. 
    - Modify #BatchDragonModel.script property
        - u_texture -> pick exported png texture in custom_res
    - Adjust the added go transform to fit your needs. Note the camera view.
    

- Add .script file to your collection. Add the following content. Modify the paths to point to the correct custom resource folder.
   ```
        --go.property("my_texture", resource.texture()) --custom texture for swapping
        local module_instance = require("dragonbones.models.instance")

    
        local skeleton_json = "/custom_res/character4/Bicycle_ske.json"
        local tex_json = "/custom_res/character4/Bicycle_tex.json"

        function init(self)
            if profiler then
               profiler.enable_ui(true)
               profiler.set_ui_view_mode(profiler.VIEW_MODE_MINIMIZED)
            end
            
            msg.post(".", "acquire_input_focus")

            self.instance = nil
            self.instance_name = "buffer1"
            local tbl = {
                skeleton_json = skeleton_json, 
                tex_json = tex_json,
                buffer_prefix = self.instance_name
            }
            
            -- set the correct url of added go
            msg.post("/BatchDragonModel", hash("load"), tbl)
            -- This script will received hash("loaded") message with instance_name value.
            
            --Disable or enable. It will disable the mesh and all updates will not be processed.
            --msg.post("/BatchDragonModel", hash("disable"), {disable = true})

           
        end

        function on_message(self, message_id, message)
            if message_id == hash("loaded") then
                --Handle specific instance, the provided name is the supply name.
                if message.instance_name == self.instance_name then
                    local instance = module_instance.instances[message.instance_name]
                    timer.delay(1/30.0, true, function()
                        -- set the correct url of added go
                        msg.post("/BatchDragonModel", hash("update"), { dt = 1/30.0 })
                    end)
                    --called dragonbones.* functions
                    --or go.* functions on the added go

                    --SIMPLE TEXTURE SWAPPING
                    --local tex_name = "my_unique_texture_id"
                    --module_instance.textures[tex_name] = hash(self.my_texture) -- from go.property
                    --msg.post("/BatchDragonModel", hash("swap_texture"), {texture_name = tex_name})

                    --tint the character
                    --msg.post("/BatchDragonModel", hash("tint"), {tint = vmath.vector4(1.0, 0.0, 0.0, 1.0)})
                end
            end
        end


    ```


- Build


**Notes**

Dont call dragonbones.update or dragonbones.create directly. Send messages instead.  
Other dragonbones.* methods can be used directly. Make sure to pass instance as the first parameter.


**Documentation**  

**Dragonbones namespace**
<pre>

function get_anination_names(instance)
    Get a table of animations defined.
     
function fade_in_animation(instance, animation_name, layer, loop, fade_in_time)
    Play the specify animation
    animation_name must be in get_aimation_names, layer can be 0,
    loop: -1 means use default value, 0 means loop infinitely, [1~N] times
    fade_in_time : time to blend to this animation

function contains_point(instance, x, y)
    Return the slot name if the given x,y lie in that slot. Translate x,y before pass it in.

function set_bone_position(instance, str_bone_name, x, y, bool_override)
    set bone position manually, for IK?

function set_bone_rotation(instance, str_bone_name, rad_angle, bool_override)
    set bone rotation in radians

function reset_bone(instance, str_bone_name)
    reset bone transforms

function stop_animation(instance, str_animation_name)
    stop the given running animation

function set_slot_visibility(instance, slot_name, bool_visible)
    Hide or Show the slot with slot_name.

function set_slot_display_index(instance, slot_name, index)
    Switch slot display image. Bounds check done internally in dragonbones?

function add_event_callback(instance, function(self, tbl_event) end)
    Receive events. Callback added. There are extra tables ints, floats and strings for custom events. pprint this table for full parameters.

function enable_event_callback(instance, bool_enable)
    Enable or disable the added event callback.

function set_flip_x(instance, bool_flip)
    flip left or right

function set_flip_y(instance, bool_flip)
    flip up or down

function get_frame_rate(instance)
    frame rate e.g 24

function replace_skin(instance, armature_name)
    No tested.
    Build a new armature and replace current armature skin from the build one.

function set_bone_scale(instance, bone_name, scale_x, scale_y)
    Scale the given bone. Addictive offset mode.

function set_slot_scale(instance, slot_name, scale_x, scale_y)
    Scale the given slot.

</pre>

**BatchDragonModel Messages**
<pre>
    hash("load")
        Message format = {
            skeleton_json = string  --valid path
            tex_json = string  
            buffer_prefix = string --unique
        }  
        Return
            Message format = {
                 instance_name = string -- buffer_prefix from incoming
            } 
            module_instance.instances[buffer_prefix] state changed.  

    hash("update")
      Message format = {  
        dt = number -- advance time by given delta e.g 1/60
      }  

    hash("disable")
        Message format = {
            disable = bool  
        }  

    hash("tint")
        Message format = {
            tint = vmath.vector4
        }
    
    hash("swap_texture")
        Message format = {
            texture_name = string
        }
        module_instance.textures[texture_name] must have valid texture.
        Obtain from resource.*
</pre>
    