
add_executable(instancing
  main.cpp
  App.cpp
  Renderer.cpp
  HeightmapGenerator.cpp
  TerrainGui.cpp
  TerrainTextures.cpp
  WorldRenderer.cpp
  RockRenderer.cpp
)

target_link_libraries(instancing
  PRIVATE glfw etna glm::glm wsi gui scene tinygltf)

target_compile_definitions(instancing
  PRIVATE INSTANCING_TEXTURES_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/textures/")

target_add_shaders(instancing
  shaders/rocks.vert
  shaders/rocks.frag
  shaders/heightmap.comp
  shaders/static_mesh.frag
  shaders/static_mesh.vert
)
