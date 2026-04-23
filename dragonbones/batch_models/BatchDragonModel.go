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
