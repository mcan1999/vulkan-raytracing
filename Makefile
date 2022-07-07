CFLAGS = -std=c++17
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

all: CFLAGS += -O2
all: main shader

debug: CFLAGS += -Og -g
debug: main shader

main: $(SRC) $(INC)
		g++ $(CFLAGS) -I $(INC_DIR) -o main $(SRC)  $(LDFLAGS)

shader: $(SHADER_FILES)
	@$(foreach file, $(wildcard $(SHADER_FILES)), glslangValidator --target-env $(VULKAN_VERSION) -o $(SHADER_DIR)/$(shell basename $(file)).spv $(file);)

clean:
	rm shaders/*
	rm main
