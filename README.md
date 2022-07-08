# Real-time Ray Tracing in Vulkan

Implementation of a real time ray tracing model with reflective and refractive objects as a project of [CENG 469 Computer Graphics II](https://catalog.metu.edu.tr/course.php?course_code=5710469) course. For the implementation, [Vulkan Ray Tracing Minimal Abstraction](https://github.com/WilliamLewww/vulkan_ray_tracing_minimal_abstraction) is used as a codebase.

## Requirements 

Required libraries for this project and how to install them  are given below:
- Vulkan SDK: 
```
sudo apt install vulkan-tools
sudo apt install libvulkan-dev
sudo apt install vulkan-validationlayers-dev spirv-tools
```
You can also test the installation with the `vkcube` command. 
- glslangValidator:
```
sudo apt install glslang-tools
sudo apt install glslang-dev
```
  You can also test the installation with the `glslangValidator` command.
- GLFW:
``` 
sudo apt install libglfw3
sudo apt install libglfw3-dev
```
- GLM: 
```
sudo apt install libglm-dev
```
## Compile & Run
You can compile and run the project by using the following commands:
```
make
./main
```
