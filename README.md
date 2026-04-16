**USAGE**
- The DragonBones Editor can be obtained from [Dragon Bones Editor](https://web.archive.org/web/20211013162034/http://tool.egret-labs.org/DragonBonesPro/DragonBonesPro-v5.6.3.exe)

- In Defold create a project and add custom resource folder hereby called custom_res/ and update game.project' custom resource entry.

- In DragonBones Editor export your animations to custom_res/ folder. Export texture in powers of 2.  Make sure three files are exported to custom resource folder. Two jsons and one png.  

- Add camera to your collection. Set camera properties such as zoom, orthographic etc.

- Add DragonModel.go or BatchDragonModel.go to your collection. The Batch version batched all the slots thereby reduce draw calls to 1. The non-batch version rendered the same way as dragon bones samples. Make sure your armature canvas in DragonBones Editor match game.project display width and height otherwise there will be skewing.  
    - Modify #DragonModel.script or #BatchDragonModel.script property
        - u_texture -> pick exported png texture in custom_res
    - Adjust the added go transform to fit your needs. Note the camera view.
    


- Add .script file to your collection. Add the following content. Modify the paths to point to the correct custom resource folder.
   ```
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
            -- This script will received hash("loaded") message with instance_no value.
            
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
                end
            end
        end


    ```


- Build


**Notes**

Dont call dragonbones.update or dragonbones.create directly. Send messages instead.  
Other dragonbones.* methods can be used directly. Make sure to pass instance as the first parameter.

**Texture Bleeding**
- Solution 1:  
    Extrude texture borders. Currently this does not solve the issue.
    There is a python script extrude.py to do just that.
    Make sure to install PIL  
        ```pip install pillow```

    - In Dragon Editor Texture config  
        - export as texture atlas first
            settings:  
                padding x and y = 4  
                powers of 2
        - export as images to the same folder.  
        - Update and run extrude.py  
            - set padding  
            - set input folder  
            - set armature name

+ Solution 2:  
Ship a tiny sprite in BatchDragonModel.  
WHY DOES THIS SOLVE TEXTURE BLEEDING?   
    - Maybe due to dragobones.material using tile tag?

+ Todo:  
Find the root cause.


**Documentation**

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


</pre>
    