
add_executable(terrain
  main.cpp
  App.cpp
  Renderer.cpp
  HeightmapGenerator.cpp
  TerrainGui.cpp
  TerrainTextures.cpp
  WorldRenderer.cpp
)

target_link_libraries(terrain
  PRIVATE glfw etna glm::glm wsi gui scene tinygltf)

target_compile_definitions(terrain
  PRIVATE TERRAIN_TEXTURES_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/textures/")

target_add_shaders(terrain
  shaders/heightmap.comp
  shaders/static_mesh.frag
  shaders/static_mesh.vert
)
