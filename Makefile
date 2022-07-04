CFLAGS = -std=c++17 -O2
LDFLAGS = -lglfw -lvulkan -ldl -lpthread -lX11 -lXxf86vm -lXrandr -lXi

SRC_DIR := src
INC_DIR := include
SHADER_DIR := shaders

SRC := $(wildcard $(SRC_DIR)/main.cpp)
INC := $(wildcard $(INC_DIR)/*.h)
SHADER_FILES := $(wildcard $(SRC_DIR)/shader*)

VULKAN_VERSION = vulkan1.3


default_target: all
.PHONY : default_target

all: main shader

main: $(SRC) $(INC)
		g++ $(CFLAGS) -I $(INC_DIR) -o main $(SRC)  $(LDFLAGS)

shader: $(SHADER_FILES)
	@$(foreach file, $(wildcard $(SHADER_FILES)), glslangValidator --target-env $(VULKAN_VERSION) -o $(SHADER_DIR)/$(shell basename $(file)).spv $(file);)

clean:
	rm shaders/*
	rm main
