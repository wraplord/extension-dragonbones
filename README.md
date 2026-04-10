**USAGE**
- In DragonBones Editor export your animations to custom_res/ folder. Export texture in powers of 2.  Make sure three files are exported to custom resource folder. Two jsons and one png.  
In Defold create an atlas with *_tex.png. Set atlas Extrude borders to 0. IMPORTANT.  

- Add DragonModel.go to your collection. 
    - Modify #DragonModel properties such as    
        - viewport -> set dimensions  
        - u_texture -> set to the created atlas 


- In your .script file
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
            
            msg.post("/DragonModel", hash("load"), tbl)

            
        end

        local function update_world(self)
            timer.delay(1/30.0, true, function()
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
                        dragonbones.set_world_translation(module_instance.instance, x, 100)
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

Dont call dragonbones.update or dragonbones.create directly. Send messages instead. Other dragonbones.* methods can be used directly.


A lot of meshes and resources are created per each character. This leads to a lot of draw calls. See [DragonModel.script](/dragonbones/models/DragonModel.scripttest.script)
Todo batch all the slots vertices and use vertex attributes for slot transform instead of vertex constant per slot

**Documentation**
<pre>
function resize(instance, viewportWidth, viewportHeight)
    update viewport width and height. 

function destroy(instance)
    destroy this world. 

  
function get_no_slots(instance)
    Get the available slots in this world. You can use this to
    create specific no of meshes.


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


function set_world_translation(instance, x, y)
    set world translation

function override_bone_position(instance, bone_name, x, y)
    set bone position manually, for IK?

function reset_bone(instance, bone_name)
    reset bone transforms

function stop_animation(instance, animation_name)
    stop the given running animation

function debug_draw(instance, debug?)  
    Show debug lines
</pre>
    


      

