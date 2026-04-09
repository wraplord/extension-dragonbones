components {
  id: "DragonModel"
  component: "/dragonbones/models/DragonModel.script"
  properties {
    id: "u_texture"
    value: "/builtins/graphics/particle_blob.tilesource"
    type: PROPERTY_TYPE_HASH
  }
}
embedded_components {
  id: "factory"
  type: "factory"
  data: "prototype: \"/dragonbones/models/DragonMeshFactory.go\"\n"
  ""
}
