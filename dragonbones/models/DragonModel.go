components {
  id: "DragonModel"
  component: "/dragonbones/models/DragonModel.script"
  properties {
    id: "u_texture"
    value: "/custom_res/character4/Bicycle_tex.atlas"
    type: PROPERTY_TYPE_HASH
  }
}
embedded_components {
  id: "factory"
  type: "factory"
  data: "prototype: \"/dragonbones/models/DragonMeshFactory.go\"\n"
  ""
}
