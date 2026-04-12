**USAGE**
- In DragonBones Editor export your animations to custom_res/ folder. Export texture in powers of 2.  Make sure three files are exported to custom resource folder. Two jsons and one png.  


- Add DragonModel.go or BatchDragonModel.go to your collection. The Batch version batched all the slots thereby reduce draw calls to 1. The non-batch version rendered the same way as dragon bones samples.
    - Modify #DragonModel.script or #BatchDragonModel.script properties  
        - viewport -> set dimensions  
        - u_texture -> pick the png texture


- In your .script file modify the paths to point to the correct custom resource folder.
   ```
        local module_instance = require("dragonbones.models.instance")

        local skeleton_json = "/custom_res/character4/Bicycle_ske.json"
        local tex_json = "/custom_res/character4/Bicycle_tex.json"

        function init(self)
            profiler.enable_ui(true)
            profiler.set_ui_view_mode(profiler.VIEW_MODE_MINIMIZED)
            
            msg.post(".", "acquire_input_focus")

            self.instance = nil
            local tbl = {
                skeleton_json = skeleton_json, 
                tex_json = tex_json,
            }
            
            --set the correct url, for batch /BatchDragonModel
            msg.post("/DragonModel", hash("load"), tbl)

            
        end

        local function update_world(self)
            timer.delay(1/30.0, true, function()
                --set the correct url, for batch /BatchDragonModel
                msg.post("/DragonModel", hash("update"), { dt = 1/30.0 })
            end)
        end

        function on_message(self, message_id, message)
            if message_id == hash("loaded") then
                update_world(self)
                self.instance = module_instance.instances[message.instance_no]
                if self.instance then
                    local x = 100
                    timer.delay(1/30, true, function()
                        x = x - 5
                        dragonbones.set_world_translation(self.instance, x, 100)
                    end) --test culling
                    
                end
            end
        end


    ```


- Build


**Todo**  
   Use editor scripts to auto link

**Notes**

The DragonBones Editor can be obtained from [Dragon Bones Editor](https://web.archive.org/web/20211013162034/http://tool.egret-labs.org/DragonBonesPro/DragonBonesPro-v5.6.3.exe)

Dont call dragonbones.update or dragonbones.create directly. Send messages instead.  
Other dragonbones.* methods can be used directly.

**Texture Bleeding**
Solution: Extrude texture borders.
There is a python script extrude.py to do just that.
Make sure to install PIL
    ```pip install pillow```

in Dragon Editor Texture config 
    - export as texture atlas first
       settings: 
            padding x and y = 4
            powers of 2
    - export as images to the same folder.
Update and run extrude.py
 + set padding
 + set input folder
 + set armature name


**Documentation**

<pre>
function resize(instance, viewportWidth, viewportHeight)
    update viewport width and height. 

function get_anination_names(instance)
    Get a table of animations defined.
     
    
function fade_in_animation(instance, animation_name, layer, loop, fade_in_time)
    Play the specify animation
    animation_name must be in get_aimation_names, layer can be 0,
    loop: -1 means use default value, 0 means loop infinitely, [1~N] times
    fade_in_time : time to blend to this animation

function contains_point(instance, x, y)
    Return the slot name if the given x,y lie in that slot


function set_world_scale(instance, scale)
    Set world scale. Use this function and set_world_translation to manage world transform

function scale(instance, scale)
    alias for set_world_scale

function set_world_translation(instance, x, y)
    set world translation

function move(instance, x, y)
    alias for set_world_translation

function set_bone_position(instance, bone_name, x, y)
    set bone position manually, for IK?

function set_bone_rotation(instance, bone_name, angle)
    set bone rotation in radians

function reset_bone(instance, bone_name)
    reset bone transforms

function stop_animation(instance, animation_name)
    stop the given running animation

function debug_draw(instance, debug?)  
    Show debug lines

function set_visible(instance, slot_name, bool_visible)
    Hide or Show the slot with slot_name.

function add_event_callback(instance, function(self, tbl_event) end)
    Receive events

function remove_event_callback(instance)
    stop receiving events 

function set_flip_x(instance, bool_flip)
    flip left or right

function set_flip_y(instance, bool_flip)
    flip up or down

function get_frame_rate(instance)
    frame rate e.g 24


</pre>
    