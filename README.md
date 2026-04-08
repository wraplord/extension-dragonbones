**USAGE**
- Export your animations to custom_res/ folder. Make sure the three files are within this custom resource folder.
- Add DragonModel.go to your collection. 
    - Modify #mesh u_texture to point your *_tex.png
    - Modify #DragonModel properties such as viewport dimensions

- Send messages to "/DragonModel" 
    - load your exported data.
    ```
        local tbl = {
            skeleton_json = "/custom_res/path_to_ske.json", 
            tex_json = "/custom_res/path_to_tex.json",
            tex_png = "/custom_res/path_to_tex.png",
        }
        msg.post("/DragonModel", "load", tbl)
    ```
    
    - instance
    ```
        msg.post("/DragonModel", "instance")

        function on_message(self, message_id, message, sender)
	        if message_id == hash("instance") then   
                self.instance = message.instance
                --update dragonbones e.g
                dragonbones.set_world_translation(self.instance, 0, 100)
            end
        end
    ```

    - update.
    ```
        local tbl = {
            dt = 1/30,
        }
        msg.post("/DragonModel", "update", tbl)
    ```

- Build


**Todo**

Use editor scripts to auto link



**Notes**

The DragonBones Editor can be obtained from [Dragon Bones Editor](https://web.archive.org/web/20211013162034/http://tool.egret-labs.org/DragonBonesPro/DragonBonesPro-v5.6.3.exe)

Dont call dragonbones.update or dragonbones.init directly. Send messages instead. Other dragonbones.* methods can be used directly.


A lot of meshes and resources are created per each character. This leads to a lot of draw calls. See [DragonModel.script](/dragonbones/models/DragonModel.scripttest.script)
