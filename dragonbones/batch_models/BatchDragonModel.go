components {
  id: "BatchDragonModel"
  component: "/dragonbones/batch_models/BatchDragonModel.script"
  properties {
    id: "u_texture"
    value: "/builtins/graphics/particle_blob.png"
    type: PROPERTY_TYPE_HASH
  }
}
embedded_components {
  id: "factory"
  type: "factory"
  data: "prototype: \"/dragonbones/batch_models/BatchDragonMeshFactory.go\"\n"
  ""
}
embedded_components {
  id: "sprite"
  type: "sprite"
  data: "default_animation: \"anim\"\n"
  "material: \"/builtins/materials/sprite.material\"\n"
  "size {\n"
  "  x: 1.0\n"
  "  y: 1.0\n"
  "}\n"
  "size_mode: SIZE_MODE_MANUAL\n"
  "textures {\n"
  "  sampler: \"texture_sampler\"\n"
  "  texture: \"/builtins/graphics/particle_blob.tilesource\"\n"
  "}\n"
  ""
}
