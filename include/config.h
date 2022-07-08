#ifndef __CONFIG_H__
#define __CONFIG_H__

#define SKYBOX_TEXTURE_DIR "resources/skybox_texture_sea"

#define CENTER_MESH_OBJ_PATH "resources/teapot.obj"
#define ORBITING_MESH_OBJ_PATH "resources/armadillo.obj"

/*
    Object types:
    0 - diffuse
    1 - mirror
    2 - refractive
*/
#define CENTER_MESH_TYPE 1
#define ORBITING_MESH_TYPE 0

// Define TEST_FPS to disable frame-rate locking and print FPS
// #define TEST_FPS

#endif