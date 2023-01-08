cmake_minimum_required(VERSION 3.22.0)

project(dji-moonlight-shim C)
add_executable(dji-moonlight-shim
  src/dmi/dji_media.c
  src/dmi/dmi_pb.c
  src/dmi/shram.c
  src/gfx.c
  src/hw/dji_display.c
  src/hw/dji_services.c
  src/main.c
)

target_compile_features(dji-moonlight-shim PRIVATE c_std_17)
target_compile_options(dji-moonlight-shim PRIVATE -O3)
target_include_directories(dji-moonlight-shim PRIVATE ./include)
target_link_directories(dji-moonlight-shim PRIVATE ./lib)
target_link_libraries(dji-moonlight-shim duml_hal png)
