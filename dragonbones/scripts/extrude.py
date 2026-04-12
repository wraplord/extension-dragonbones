import json
from PIL import Image

#--------------------
input_folder = "C:/Users/admin/Desktop/MustDragon/extension-dragonbones/custom_res/character3/"
armature_name = "Yah"
padding = 2
ext = ".png"
#-----------------------------

images_folder = input_folder + armature_name + "_texture/"
tex_json = input_folder + armature_name +  "_tex.json"

data = json.loads(open(tex_json, encoding="utf8").read())
width = data["width"]
height = data["height"]

atlas = Image.new("RGBA", (width, height))

array = data["SubTexture"]
for arr in array:
   w = arr["width"]
   y = arr["y"]
   h = arr["height"]
   name = arr["name"]
   x    = arr["x"]

   xsize = w + padding * 2
   ysize = h + padding * 2

   image = Image.open(images_folder + name + ext)
   image.resize((xsize, ysize))
   atlas.paste(image, (x - padding, y - padding))

atlas_tex = input_folder + armature_name +  "_tex_padded.png"
atlas.save(atlas_tex, "PNG")