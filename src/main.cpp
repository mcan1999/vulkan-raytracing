#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <fstream>
#include <iostream>
#include <vector>

#define STRING_RESET "\033[0m"
#define STRING_INFO "\033[37m"
#define STRING_WARNING "\033[33m"
#define STRING_ERROR "\033[36m"

#define PRINT_MESSAGE(stream, message) stream << message << std::endl;

static char keyDownIndex[500];

static float cameraPosition[3];
static float cameraYaw;
static float cameraPitch;

//function pointers
PFN_vkGetBufferDeviceAddressKHR pvkGetBufferDeviceAddressKHR;
PFN_vkCreateRayTracingPipelinesKHR pvkCreateRayTracingPipelinesKHR;
PFN_vkGetAccelerationStructureBuildSizesKHR pvkGetAccelerationStructureBuildSizesKHR;
PFN_vkCreateAccelerationStructureKHR pvkCreateAccelerationStructureKHR;
PFN_vkDestroyAccelerationStructureKHR pvkDestroyAccelerationStructureKHR;
PFN_vkGetAccelerationStructureDeviceAddressKHR pvkGetAccelerationStructureDeviceAddressKHR;
PFN_vkCmdBuildAccelerationStructuresKHR pvkCmdBuildAccelerationStructuresKHR;
PFN_vkGetRayTracingShaderGroupHandlesKHR pvkGetRayTracingShaderGroupHandlesKHR;
PFN_vkCmdTraceRaysKHR pvkCmdTraceRaysKHR;

//globals
VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties;
VkDevice deviceHandle;

void parseFile( tinyobj::ObjReaderConfig& reader_config, tinyobj::ObjReader& reader, const char * fileName){
  
  if (!reader.ParseFromFile(fileName, reader_config)) {
    if (!reader.Error().empty()) {
      std::cerr << "TinyObjReader: " << reader.Error();
    }
    exit(1);
  }

  if (!reader.Warning().empty()) {
    std::cout << "TinyObjReader: " << reader.Warning();
  }
}

void keyCallback(GLFWwindow *windowPtr, int key, int scancode, int action,
                 int mods) {

  if (action == GLFW_PRESS) {
    keyDownIndex[key] = 1;
  }

  if (action == GLFW_RELEASE) {
    keyDownIndex[key] = 0;
  }
}

VkBool32
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT messageTypes,
              const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
              void *pUserData) {

  std::string message = pCallbackData->pMessage;

  if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    message = STRING_INFO + message + STRING_RESET;
    PRINT_MESSAGE(std::cout, message.c_str());
  }

  if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    message = STRING_WARNING + message + STRING_RESET;
    PRINT_MESSAGE(std::cerr, message.c_str());
  }

  if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    message = STRING_ERROR + message + STRING_RESET;
    PRINT_MESSAGE(std::cerr, message.c_str());
  }

  return VK_FALSE;
}

void throwExceptionVulkanAPI(VkResult result, std::string functionName) {
  std::string message = "Vulkan API exception: return code " +
                        std::to_string(result) + " (" + functionName + ")";

  throw std::runtime_error(message);
}

void throwExceptionMessage(std::string message) {
  throw std::runtime_error(message);
}

//************** Helpers ****************************
uint32_t getMemoryIndex(VkMemoryRequirements& memoryRequirements,
  VkMemoryPropertyFlagBits memoryFlagBits)
{
  uint32_t memoryIndex = -1;
  for (uint32_t x = 0; x < physicalDeviceMemoryProperties.memoryTypeCount;
       x++) {

    if ((memoryRequirements.memoryTypeBits &
         (1 << x)) &&
        (physicalDeviceMemoryProperties.memoryTypes[x].propertyFlags &
         memoryFlagBits) ==
            memoryFlagBits) {

      memoryIndex = x;
      break;
    }
  }

  return memoryIndex;
}

void allocAndBind(VkDeviceMemory& deviceMemoryHandle,
  VkMemoryAllocateFlagsInfo* memoryAllocateFlagsInfoPtr,
  VkBuffer& bufferHandle,
  VkMemoryPropertyFlagBits memoryFlagBits)
{
  VkMemoryRequirements memoryRequirements;
  vkGetBufferMemoryRequirements(deviceHandle, bufferHandle,
                                &memoryRequirements);

  uint32_t memoryTypeIndex = getMemoryIndex(memoryRequirements, memoryFlagBits);
  
  VkMemoryAllocateInfo memoryAllocateInfo = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = memoryAllocateFlagsInfoPtr,
      .allocationSize = memoryRequirements.size,
      .memoryTypeIndex = memoryTypeIndex};


  deviceMemoryHandle = VK_NULL_HANDLE;
  VkResult result = vkAllocateMemory(deviceHandle, &memoryAllocateInfo, NULL,
                            &deviceMemoryHandle);
  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkAllocateMemory");
  }

  result = vkBindBufferMemory(deviceHandle, bufferHandle,
                              deviceMemoryHandle, 0);
  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkBindBufferMemory");
  }
}

void copyData(VkDeviceMemory &deviceMemoryHandle,
  void* data,
  VkDeviceSize dataSize){
  void *hostVertexMemoryBuffer;
  VkResult result = vkMapMemory(deviceHandle, deviceMemoryHandle, 0,
                       dataSize, 0,
                       &hostVertexMemoryBuffer);

  memcpy(hostVertexMemoryBuffer, data, dataSize);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkMapMemory");
  }

  vkUnmapMemory(deviceHandle, deviceMemoryHandle);
}

void createBuffer(VkBuffer& bufferHandle,
  VkDeviceSize bufferSize,
  VkBufferUsageFlags usageFlags,
  uint32_t queueFamilyIndex)
{
  VkBufferCreateInfo bufferCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    .size = bufferSize,
    .usage = usageFlags,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 1,
    .pQueueFamilyIndices = &queueFamilyIndex};

  bufferHandle = VK_NULL_HANDLE;
  VkResult result = vkCreateBuffer(deviceHandle, &bufferCreateInfo, NULL,
                          &bufferHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateBuffer");
  }
}
//*****************************************************

void createVertexBuffer(VkBuffer& vertexBufferHandle, 
  const tinyobj::attrib_t &attrib,
  uint32_t& queueFamilyIndex, 
  VkMemoryAllocateFlagsInfo& memoryAllocateFlagsInfo,
  VkDeviceMemory& vertexDeviceMemoryHandle,
  VkDeviceAddress& vertexBufferDeviceAddress)
{
  vertexBufferHandle = VK_NULL_HANDLE;
  createBuffer(vertexBufferHandle,
    sizeof(float) * attrib.vertices.size() * 3,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    queueFamilyIndex);

  allocAndBind(vertexDeviceMemoryHandle,
    &memoryAllocateFlagsInfo,
    vertexBufferHandle,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

  copyData(vertexDeviceMemoryHandle,
    (void *) attrib.vertices.data(),
    sizeof(float) * attrib.vertices.size() * 3);

  VkBufferDeviceAddressInfo vertexBufferDeviceAddressInfo = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
      .pNext = NULL,
      .buffer = vertexBufferHandle};

  vertexBufferDeviceAddress = pvkGetBufferDeviceAddressKHR(
      deviceHandle, &vertexBufferDeviceAddressInfo);
}

void createIndexBuffer(VkBuffer& indexBufferHandle, 
  std::vector<uint32_t>& indexList,
  uint32_t& queueFamilyIndex,
  VkMemoryAllocateFlagsInfo& memoryAllocateFlagsInfo,
  VkDeviceMemory& indexDeviceMemoryHandle,
  VkDeviceAddress& indexBufferDeviceAddress)
{
  createBuffer(indexBufferHandle,
    sizeof(uint32_t) * indexList.size(),
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    queueFamilyIndex);
  
  allocAndBind(indexDeviceMemoryHandle,
    &memoryAllocateFlagsInfo,
    indexBufferHandle,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  
  copyData(indexDeviceMemoryHandle,
    (void *) indexList.data(),
    sizeof(uint32_t) * indexList.size());

  VkBufferDeviceAddressInfo indexBufferDeviceAddressInfo = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
      .pNext = NULL,
      .buffer = indexBufferHandle};

  indexBufferDeviceAddress =
      pvkGetBufferDeviceAddressKHR(deviceHandle, &indexBufferDeviceAddressInfo);
}

void createBLASGeometry(VkAccelerationStructureGeometryKHR& bottomLevelAccelerationStructureGeometry,
  VkDeviceAddress vertexBufferDeviceAddress,
  VkDeviceAddress indexBufferDeviceAddress,
  const tinyobj::attrib_t& attrib)
{

  VkAccelerationStructureGeometryDataKHR
      bottomLevelAccelerationStructureGeometryData = {
          .triangles = {
              .sType =
                  VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
              .pNext = NULL,
              .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
              .vertexData = {.deviceAddress = vertexBufferDeviceAddress},
              .vertexStride = sizeof(float) * 3,
              .maxVertex = (uint32_t)attrib.vertices.size(),
              .indexType = VK_INDEX_TYPE_UINT32,
              .indexData = {.deviceAddress = indexBufferDeviceAddress},
              .transformData = {.deviceAddress = 0}}};

  bottomLevelAccelerationStructureGeometry =
      {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
       .pNext = NULL,
       .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
       .geometry = bottomLevelAccelerationStructureGeometryData,
       .flags = VK_GEOMETRY_OPAQUE_BIT_KHR};
}

void createBLAS(VkAccelerationStructureKHR& bottomLevelAccelerationStructureHandle,
  VkAccelerationStructureGeometryKHR& bottomLevelAccelerationStructureGeometry,
  uint32_t primitiveCount,
  uint32_t& queueFamilyIndex,
  VkBuffer& bottomLevelAccelerationStructureBufferHandle,
  VkDeviceMemory& bottomLevelAccelerationStructureDeviceMemoryHandle,
  VkAccelerationStructureBuildSizesInfoKHR& bottomLevelAccelerationStructureBuildSizesInfo,
  VkAccelerationStructureBuildGeometryInfoKHR& bottomLevelAccelerationStructureBuildGeometryInfo
  )
{
  VkResult result;
  
  bottomLevelAccelerationStructureBuildGeometryInfo = {
    .sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
    .pNext = NULL,
    .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
    .flags = 0, //TODO check flags VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR
    .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
    .srcAccelerationStructure = VK_NULL_HANDLE,
    .dstAccelerationStructure = VK_NULL_HANDLE,
    .geometryCount = 1,
    .pGeometries = &bottomLevelAccelerationStructureGeometry,
    .ppGeometries = NULL,
    .scratchData = {.deviceAddress = 0}}; //TODO check


  bottomLevelAccelerationStructureBuildSizesInfo = {
    .sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    .pNext = NULL,
    .accelerationStructureSize = 0,
    .updateScratchSize = 0,
    .buildScratchSize = 0};

  std::vector<uint32_t> bottomLevelMaxPrimitiveCountList = {primitiveCount};

  pvkGetAccelerationStructureBuildSizesKHR(
      deviceHandle, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
      &bottomLevelAccelerationStructureBuildGeometryInfo,
      bottomLevelMaxPrimitiveCountList.data(),
      &bottomLevelAccelerationStructureBuildSizesInfo);

  //Create buffer
  bottomLevelAccelerationStructureBufferHandle = VK_NULL_HANDLE;
  createBuffer(bottomLevelAccelerationStructureBufferHandle,
    bottomLevelAccelerationStructureBuildSizesInfo.accelerationStructureSize,
    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
    queueFamilyIndex);

  allocAndBind(bottomLevelAccelerationStructureDeviceMemoryHandle,
    NULL,
    bottomLevelAccelerationStructureBufferHandle,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  VkAccelerationStructureCreateInfoKHR
      bottomLevelAccelerationStructureCreateInfo = {
          .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
          .pNext = NULL,
          .createFlags = 0,
          .buffer = bottomLevelAccelerationStructureBufferHandle,
          .offset = 0,
          .size = bottomLevelAccelerationStructureBuildSizesInfo
                      .accelerationStructureSize,
          .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
          .deviceAddress = 0};

  bottomLevelAccelerationStructureHandle = VK_NULL_HANDLE;

  result = pvkCreateAccelerationStructureKHR(
      deviceHandle, &bottomLevelAccelerationStructureCreateInfo, NULL,
      &bottomLevelAccelerationStructureHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateAccelerationStructureKHR");
  }
}


void createBLASScratchBuffer(VkBuffer& bottomLevelAccelerationStructureScratchBufferHandle,
  VkAccelerationStructureKHR& bottomLevelAccelerationStructureHandle,
  VkDeviceAddress& bottomLevelAccelerationStructureDeviceAddress,
  VkAccelerationStructureBuildSizesInfoKHR& bottomLevelAccelerationStructureBuildSizesInfo,
  uint32_t& queueFamilyIndex,
  VkMemoryAllocateFlagsInfo& memoryAllocateFlagsInfo,
  VkDeviceMemory& bottomLevelAccelerationStructureDeviceScratchMemoryHandle,
  VkAccelerationStructureBuildGeometryInfoKHR& bottomLevelAccelerationStructureBuildGeometryInfo
){
  VkAccelerationStructureDeviceAddressInfoKHR
      bottomLevelAccelerationStructureDeviceAddressInfo = {
          .sType =
              VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
          .pNext = NULL,
          .accelerationStructure = bottomLevelAccelerationStructureHandle};
  
  bottomLevelAccelerationStructureDeviceAddress =
      pvkGetAccelerationStructureDeviceAddressKHR(
          deviceHandle, &bottomLevelAccelerationStructureDeviceAddressInfo);

  bottomLevelAccelerationStructureScratchBufferHandle = VK_NULL_HANDLE;
  createBuffer(bottomLevelAccelerationStructureScratchBufferHandle,
    bottomLevelAccelerationStructureBuildSizesInfo.buildScratchSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    queueFamilyIndex);

  allocAndBind(bottomLevelAccelerationStructureDeviceScratchMemoryHandle,
    &memoryAllocateFlagsInfo,
    bottomLevelAccelerationStructureScratchBufferHandle,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  VkBufferDeviceAddressInfo
    bottomLevelAccelerationStructureScratchBufferDeviceAddressInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .pNext = NULL,
        .buffer = bottomLevelAccelerationStructureScratchBufferHandle};

  VkDeviceAddress bottomLevelAccelerationStructureScratchBufferDeviceAddress =
      pvkGetBufferDeviceAddressKHR(
          deviceHandle,
          &bottomLevelAccelerationStructureScratchBufferDeviceAddressInfo);
  
  bottomLevelAccelerationStructureBuildGeometryInfo.dstAccelerationStructure =
      bottomLevelAccelerationStructureHandle;

  bottomLevelAccelerationStructureBuildGeometryInfo.scratchData = {
      .deviceAddress =
          bottomLevelAccelerationStructureScratchBufferDeviceAddress};
}

void buildBLAS(VkCommandBuffer& commandBufferHandle,
  VkAccelerationStructureBuildRangeInfoKHR& bottomLevelAccelerationStructureBuildRangeInfo,
  VkAccelerationStructureBuildGeometryInfoKHR& bottomLevelAccelerationStructureBuildGeometryInfo,
  VkFence& bottomLevelAccelerationStructureBuildFenceHandle,
  VkDevice deviceHandle,
  VkQueue& queueHandle)
{
  
  VkCommandBufferBeginInfo bottomLevelCommandBufferBeginInfo = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext = NULL,
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    .pInheritanceInfo = NULL};

  VkResult result = vkBeginCommandBuffer(commandBufferHandle,
                              &bottomLevelCommandBufferBeginInfo);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkBeginCommandBuffer");
  }

  const VkAccelerationStructureBuildRangeInfoKHR
    *bottomLevelAccelerationStructureBuildRangeInfos =
        {&bottomLevelAccelerationStructureBuildRangeInfo};
  
  pvkCmdBuildAccelerationStructuresKHR(
    commandBufferHandle, 1,
    &bottomLevelAccelerationStructureBuildGeometryInfo,
    &bottomLevelAccelerationStructureBuildRangeInfos);

  result = vkEndCommandBuffer(commandBufferHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkEndCommandBuffer");
  }

  VkSubmitInfo bottomLevelAccelerationStructureBuildSubmitInfo = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = NULL,
      .waitSemaphoreCount = 0,
      .pWaitSemaphores = NULL,
      .pWaitDstStageMask = NULL,
      .commandBufferCount = 1,
      .pCommandBuffers = &commandBufferHandle,
      .signalSemaphoreCount = 0,
      .pSignalSemaphores = NULL};

  result = vkQueueSubmit(queueHandle, 1,
                         &bottomLevelAccelerationStructureBuildSubmitInfo,
                         bottomLevelAccelerationStructureBuildFenceHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkQueueSubmit");
  }

  result = vkWaitForFences(deviceHandle, 1,
                           &bottomLevelAccelerationStructureBuildFenceHandle,
                           true, UINT32_MAX);

  if (result != VK_SUCCESS && result != VK_TIMEOUT) {
    throwExceptionVulkanAPI(result, "vkWaitForFences");
  }

}

void createTLAS(VkAccelerationStructureKHR& topLevelAccelerationStructureHandle,
  VkDeviceAddress& bottomLevelAccelerationStructureDeviceAddress,
  VkTransformMatrixKHR& transformMatrix,
  uint32_t objIndex,
  uint32_t& queueFamilyIndex,
  VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo,
  VkBuffer& topLevelAccelerationStructureBufferHandle,
  VkDeviceMemory& topLevelAccelerationStructureDeviceMemoryHandle,
  VkCommandBuffer& commandBufferHandle,
  VkQueue& queueHandle,
  bool update)
{
  VkResult result;

  VkAccelerationStructureInstanceKHR bottomLevelAccelerationStructureInstance =
  {.transform = transformMatrix,
    .instanceCustomIndex = objIndex,
    .mask = 0xFF,
    .instanceShaderBindingTableRecordOffset = 0, //TODO shader binding
    .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
    .accelerationStructureReference =
        bottomLevelAccelerationStructureDeviceAddress};

  // create buffer for each tla (NV createsone buffer)
  VkBuffer bottomLevelGeometryInstanceBufferHandle = VK_NULL_HANDLE;
  createBuffer(bottomLevelGeometryInstanceBufferHandle,
    sizeof(VkAccelerationStructureInstanceKHR),
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    queueFamilyIndex);

  VkDeviceMemory bottomLevelGeometryInstanceDeviceMemoryHandle = VK_NULL_HANDLE;

  allocAndBind(bottomLevelGeometryInstanceDeviceMemoryHandle,
    &memoryAllocateFlagsInfo,
    bottomLevelGeometryInstanceBufferHandle,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  
  copyData(bottomLevelGeometryInstanceDeviceMemoryHandle,
    (void *) &bottomLevelAccelerationStructureInstance,
    sizeof(VkAccelerationStructureInstanceKHR));

  VkBufferDeviceAddressInfo bottomLevelGeometryInstanceDeviceAddressInfo = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
      .pNext = NULL,
      .buffer = bottomLevelGeometryInstanceBufferHandle};
  
  VkDeviceAddress bottomLevelGeometryInstanceDeviceAddress =
    pvkGetBufferDeviceAddressKHR(
        deviceHandle, &bottomLevelGeometryInstanceDeviceAddressInfo);

  VkAccelerationStructureGeometryDataKHR topLevelAccelerationStructureGeometryData =
    {.instances = {
          .sType =
              VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
          .pNext = NULL,
          .arrayOfPointers = VK_FALSE,
          .data = {.deviceAddress =
                      bottomLevelGeometryInstanceDeviceAddress}}};

  VkAccelerationStructureGeometryKHR topLevelAccelerationStructureGeometry = {
    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
    .pNext = NULL,
    .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
    .geometry = topLevelAccelerationStructureGeometryData,
    .flags = VK_GEOMETRY_OPAQUE_BIT_KHR};
  
  VkAccelerationStructureBuildGeometryInfoKHR
    topLevelAccelerationStructureBuildGeometryInfo = {
        .sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .pNext = NULL,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags = 0,
        .mode = update ? 
          VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : 
          VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .srcAccelerationStructure = VK_NULL_HANDLE,
        .dstAccelerationStructure = VK_NULL_HANDLE,
        .geometryCount = 1,
        .pGeometries = &topLevelAccelerationStructureGeometry,
        .ppGeometries = NULL,
          .scratchData = {.deviceAddress = 0}};

  VkAccelerationStructureBuildSizesInfoKHR
  topLevelAccelerationStructureBuildSizesInfo = {
      .sType =
          VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
      .pNext = NULL,
      .accelerationStructureSize = 0,
      .updateScratchSize = 0,
      .buildScratchSize = 0};
    
  std::vector<uint32_t> topLevelMaxPrimitiveCountList = {1};

  pvkGetAccelerationStructureBuildSizesKHR(
      deviceHandle, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
      &topLevelAccelerationStructureBuildGeometryInfo,
      topLevelMaxPrimitiveCountList.data(),
      &topLevelAccelerationStructureBuildSizesInfo);
  
  if(update == false){
    topLevelAccelerationStructureBufferHandle = VK_NULL_HANDLE;
    createBuffer(topLevelAccelerationStructureBufferHandle,
      topLevelAccelerationStructureBuildSizesInfo.accelerationStructureSize,
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, //TODO  | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ??
      queueFamilyIndex);

    topLevelAccelerationStructureDeviceMemoryHandle = VK_NULL_HANDLE;
    allocAndBind(topLevelAccelerationStructureDeviceMemoryHandle,
      NULL,
      topLevelAccelerationStructureBufferHandle,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    VkAccelerationStructureCreateInfoKHR topLevelAccelerationStructureCreateInfo =
        {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .pNext = NULL,
        .createFlags = 0,
        .buffer = topLevelAccelerationStructureBufferHandle,
        .offset = 0,
        .size = topLevelAccelerationStructureBuildSizesInfo
                    .accelerationStructureSize,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .deviceAddress = 0};
    
    topLevelAccelerationStructureHandle = VK_NULL_HANDLE;

    result = pvkCreateAccelerationStructureKHR(
        deviceHandle, &topLevelAccelerationStructureCreateInfo, NULL,
        &topLevelAccelerationStructureHandle);

    if (result != VK_SUCCESS) {
      throwExceptionVulkanAPI(result, "vkCreateAccelerationStructureKHR");
    }

  }
  
  //----------------------build----------------------

  VkAccelerationStructureDeviceAddressInfoKHR
    topLevelAccelerationStructureDeviceAddressInfo = {
        .sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .pNext = NULL,
        .accelerationStructure = topLevelAccelerationStructureHandle};
  
  VkDeviceAddress topLevelAccelerationStructureDeviceAddress =
      pvkGetAccelerationStructureDeviceAddressKHR(
          deviceHandle, &topLevelAccelerationStructureDeviceAddressInfo);
  
  VkBuffer topLevelAccelerationStructureScratchBufferHandle = VK_NULL_HANDLE;
  createBuffer(topLevelAccelerationStructureScratchBufferHandle,
    topLevelAccelerationStructureBuildSizesInfo.buildScratchSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    queueFamilyIndex);

  VkDeviceMemory topLevelAccelerationStructureDeviceScratchMemoryHandle =
    VK_NULL_HANDLE;

  allocAndBind(topLevelAccelerationStructureDeviceScratchMemoryHandle,
    &memoryAllocateFlagsInfo,
    topLevelAccelerationStructureScratchBufferHandle,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  
   VkBufferDeviceAddressInfo
      topLevelAccelerationStructureScratchBufferDeviceAddressInfo = {
          .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
          .pNext = NULL,
          .buffer = topLevelAccelerationStructureScratchBufferHandle};

  VkDeviceAddress topLevelAccelerationStructureScratchBufferDeviceAddress =
      pvkGetBufferDeviceAddressKHR(
          deviceHandle,
          &topLevelAccelerationStructureScratchBufferDeviceAddressInfo);

  topLevelAccelerationStructureBuildGeometryInfo.dstAccelerationStructure =
      topLevelAccelerationStructureHandle;

  topLevelAccelerationStructureBuildGeometryInfo.srcAccelerationStructure =
    update ? topLevelAccelerationStructureHandle : VK_NULL_HANDLE;

  topLevelAccelerationStructureBuildGeometryInfo.scratchData = {
      .deviceAddress = topLevelAccelerationStructureScratchBufferDeviceAddress};
  
  VkAccelerationStructureBuildRangeInfoKHR
    topLevelAccelerationStructureBuildRangeInfo = {.primitiveCount = 1,
                                                    .primitiveOffset = 0,
                                                    .firstVertex = 0,
                                                    .transformOffset = 0};

  const VkAccelerationStructureBuildRangeInfoKHR
      *topLevelAccelerationStructureBuildRangeInfos =
          &topLevelAccelerationStructureBuildRangeInfo;

  VkCommandBufferBeginInfo topLevelCommandBufferBeginInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = NULL,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      .pInheritanceInfo = NULL};

  result = vkBeginCommandBuffer(commandBufferHandle,
                              &topLevelCommandBufferBeginInfo);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkBeginCommandBuffer");
  }

  pvkCmdBuildAccelerationStructuresKHR(
    commandBufferHandle, 1,
    &topLevelAccelerationStructureBuildGeometryInfo,
    &topLevelAccelerationStructureBuildRangeInfos);

  result = vkEndCommandBuffer(commandBufferHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkEndCommandBuffer");
  }

  VkSubmitInfo topLevelAccelerationStructureBuildSubmitInfo = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext = NULL,
    .waitSemaphoreCount = 0,
    .pWaitSemaphores = NULL,
    .pWaitDstStageMask = NULL,
    .commandBufferCount = 1,
    .pCommandBuffers = &commandBufferHandle,
    .signalSemaphoreCount = 0,
    .pSignalSemaphores = NULL};
  
  VkFenceCreateInfo topLevelAccelerationStructureBuildFenceCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = NULL, .flags = 0};
  
  VkFence topLevelAccelerationStructureBuildFenceHandle = VK_NULL_HANDLE;
    result = vkCreateFence(deviceHandle,
                          &topLevelAccelerationStructureBuildFenceCreateInfo,
                          NULL, &topLevelAccelerationStructureBuildFenceHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateFence");
  }

  result = vkQueueSubmit(queueHandle, 1,
                         &topLevelAccelerationStructureBuildSubmitInfo,
                         topLevelAccelerationStructureBuildFenceHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkQueueSubmit");
  }

  result = vkWaitForFences(deviceHandle, 1,
                           &topLevelAccelerationStructureBuildFenceHandle, true,
                           UINT32_MAX);

  if (result != VK_SUCCESS && result != VK_TIMEOUT) {
    throwExceptionVulkanAPI(result, "vkWaitForFences");
  }

  //free resources
  vkDestroyFence(deviceHandle, topLevelAccelerationStructureBuildFenceHandle,
                NULL);

  vkDestroyBuffer(deviceHandle, topLevelAccelerationStructureScratchBufferHandle, NULL);

  vkFreeMemory(deviceHandle, topLevelAccelerationStructureDeviceScratchMemoryHandle,
               NULL);

  vkDestroyBuffer(deviceHandle, bottomLevelGeometryInstanceBufferHandle, NULL);

  vkFreeMemory(deviceHandle, bottomLevelGeometryInstanceDeviceMemoryHandle,
               NULL);
}


int main() {
  VkResult result;

  // =========================================================================
  // GLFW, Window

  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  GLFWwindow *windowPtr = glfwCreateWindow(800, 600, "Vulkan", NULL, NULL);
  glfwSetInputMode(windowPtr, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  glfwSetKeyCallback(windowPtr, keyCallback);

  // =========================================================================
  // Vulkan Instance

  std::vector<VkValidationFeatureEnableEXT> validationFeatureEnableList = {
      VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
      VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
      VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT};

  VkDebugUtilsMessageSeverityFlagBitsEXT debugUtilsMessageSeverityFlagBits =
      (VkDebugUtilsMessageSeverityFlagBitsEXT)(
          VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
          VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);

  VkDebugUtilsMessageTypeFlagBitsEXT debugUtilsMessageTypeFlagBits =
      (VkDebugUtilsMessageTypeFlagBitsEXT)(
          VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
          VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT);

  VkValidationFeaturesEXT validationFeatures = {
      .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
      .pNext = NULL,
      .enabledValidationFeatureCount =
          (uint32_t)validationFeatureEnableList.size(),
      .pEnabledValidationFeatures = validationFeatureEnableList.data(),
      .disabledValidationFeatureCount = 0,
      .pDisabledValidationFeatures = NULL};

  VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .pNext = &validationFeatures,
      .flags = 0,
      .messageSeverity = debugUtilsMessageSeverityFlagBits,
      .messageType = debugUtilsMessageTypeFlagBits,
      .pfnUserCallback = &debugCallback,
      .pUserData = NULL};

  VkApplicationInfo applicationInfo = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pNext = NULL,
      .pApplicationName = "Ray Tracing Pipeline Example",
      .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
      .pEngineName = "",
      .engineVersion = VK_MAKE_VERSION(1, 0, 0),
      .apiVersion = VK_API_VERSION_1_3};

  std::vector<const char *> instanceLayerList = {"VK_LAYER_KHRONOS_validation"};

  uint32_t glfwExtensionCount = 0;
  const char **glfwExtensions =
      glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  std::vector<const char *> instanceExtensionList(
      glfwExtensions, glfwExtensions + glfwExtensionCount);

  instanceExtensionList.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  instanceExtensionList.push_back("VK_KHR_surface");

  VkInstanceCreateInfo instanceCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = &debugUtilsMessengerCreateInfo,
      .flags = 0,
      .pApplicationInfo = &applicationInfo,
      .enabledLayerCount = (uint32_t)instanceLayerList.size(),
      .ppEnabledLayerNames = instanceLayerList.data(),
      .enabledExtensionCount = (uint32_t)instanceExtensionList.size(),
      .ppEnabledExtensionNames = instanceExtensionList.data(),
  };

  VkInstance instanceHandle = VK_NULL_HANDLE;
  result = vkCreateInstance(&instanceCreateInfo, NULL, &instanceHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateInstance");
  }

  // =========================================================================
  // Window Surface

  VkSurfaceKHR surfaceHandle = VK_NULL_HANDLE;
  result =
      glfwCreateWindowSurface(instanceHandle, windowPtr, NULL, &surfaceHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "glfwCreateWindowSurface");
  }

  // =========================================================================
  // Physical Device

  uint32_t physicalDeviceCount = 0;
  result =
      vkEnumeratePhysicalDevices(instanceHandle, &physicalDeviceCount, NULL);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkEnumeratePhysicalDevices");
  }

  std::vector<VkPhysicalDevice> physicalDeviceHandleList(physicalDeviceCount);
  result = vkEnumeratePhysicalDevices(instanceHandle, &physicalDeviceCount,
                                      physicalDeviceHandleList.data());

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkEnumeratePhysicalDevices");
  }

  VkPhysicalDevice activePhysicalDeviceHandle = physicalDeviceHandleList[0];

  VkPhysicalDeviceRayTracingPipelinePropertiesKHR
      physicalDeviceRayTracingPipelineProperties = {
          .sType =
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
          .pNext = NULL};

  VkPhysicalDeviceProperties2 physicalDeviceProperties2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = &physicalDeviceRayTracingPipelineProperties};

  vkGetPhysicalDeviceProperties2(activePhysicalDeviceHandle,
                                 &physicalDeviceProperties2);

  //init VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties
  vkGetPhysicalDeviceMemoryProperties(activePhysicalDeviceHandle,
                                      &physicalDeviceMemoryProperties);

  std::cout << "DEVICE: " << physicalDeviceProperties2.properties.deviceName << std::endl;

  // =========================================================================
  // Physical Device Features

  VkPhysicalDeviceBufferDeviceAddressFeatures
      physicalDeviceBufferDeviceAddressFeatures = {
          .sType =
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
          .pNext = NULL,
          .bufferDeviceAddress = VK_TRUE,
          .bufferDeviceAddressCaptureReplay = VK_FALSE,
          .bufferDeviceAddressMultiDevice = VK_FALSE};

  VkPhysicalDeviceAccelerationStructureFeaturesKHR
      physicalDeviceAccelerationStructureFeatures = {
          .sType =
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
          .pNext = &physicalDeviceBufferDeviceAddressFeatures,
          .accelerationStructure = VK_TRUE,
          .accelerationStructureCaptureReplay = VK_FALSE,
          .accelerationStructureIndirectBuild = VK_FALSE,
          .accelerationStructureHostCommands = VK_FALSE,
          .descriptorBindingAccelerationStructureUpdateAfterBind = VK_FALSE};

  VkPhysicalDeviceRayTracingPipelineFeaturesKHR
      physicalDeviceRayTracingPipelineFeatures = {
          .sType =
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
          .pNext = &physicalDeviceAccelerationStructureFeatures,
          .rayTracingPipeline = VK_TRUE,
          .rayTracingPipelineShaderGroupHandleCaptureReplay = VK_FALSE,
          .rayTracingPipelineShaderGroupHandleCaptureReplayMixed = VK_FALSE,
          .rayTracingPipelineTraceRaysIndirect = VK_FALSE,
          .rayTraversalPrimitiveCulling = VK_FALSE};
  VkPhysicalDeviceFeatures deviceFeatures = {.geometryShader = VK_TRUE};

  // =========================================================================
  // Physical Device Submission Queue Families

  uint32_t queueFamilyPropertyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(activePhysicalDeviceHandle,
                                           &queueFamilyPropertyCount, NULL);

  std::vector<VkQueueFamilyProperties> queueFamilyPropertiesList(
      queueFamilyPropertyCount);

  vkGetPhysicalDeviceQueueFamilyProperties(activePhysicalDeviceHandle,
                                           &queueFamilyPropertyCount,
                                           queueFamilyPropertiesList.data());

  uint32_t queueFamilyIndex = -1;
  for (uint32_t x = 0; x < queueFamilyPropertiesList.size(); x++) {
    if (queueFamilyPropertiesList[x].queueFlags & VK_QUEUE_GRAPHICS_BIT) {

      VkBool32 isPresentSupported = false;
      result = vkGetPhysicalDeviceSurfaceSupportKHR(
          activePhysicalDeviceHandle, x, surfaceHandle, &isPresentSupported);

      if (result != VK_SUCCESS) {
        throwExceptionVulkanAPI(result, "vkGetPhysicalDeviceSurfaceSupportKHR");
      }

      if (isPresentSupported) {
        queueFamilyIndex = x;
        break;
      }
    }
  }

  std::vector<float> queuePrioritiesList = {1.0f};
  VkDeviceQueueCreateInfo deviceQueueCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .queueFamilyIndex = queueFamilyIndex,
      .queueCount = 1,
      .pQueuePriorities = queuePrioritiesList.data()};

  // =========================================================================
  // Logical Device

  std::vector<const char *> deviceExtensionList = {
      "VK_KHR_ray_tracing_pipeline",
      "VK_KHR_acceleration_structure",
      "VK_EXT_descriptor_indexing",
      "VK_KHR_maintenance3",
      "VK_KHR_buffer_device_address",
      "VK_KHR_deferred_host_operations",
      "VK_KHR_swapchain"};

  VkDeviceCreateInfo deviceCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &physicalDeviceRayTracingPipelineFeatures,
      .flags = 0,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &deviceQueueCreateInfo,
      .enabledLayerCount = (uint32_t)instanceLayerList.size(),
      .ppEnabledLayerNames = instanceLayerList.data(),
      .enabledExtensionCount = (uint32_t)deviceExtensionList.size(),
      .ppEnabledExtensionNames = deviceExtensionList.data(),
      .pEnabledFeatures = &deviceFeatures};

  deviceHandle = VK_NULL_HANDLE;
  result = vkCreateDevice(activePhysicalDeviceHandle, &deviceCreateInfo, NULL,
                          &deviceHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateDevice");
  }

  // =========================================================================
  // Submission Queue

  VkQueue queueHandle = VK_NULL_HANDLE;
  vkGetDeviceQueue(deviceHandle, queueFamilyIndex, 0, &queueHandle);

  // =========================================================================
  // Device Pointer Functions

  pvkGetBufferDeviceAddressKHR =
      (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(
          deviceHandle, "vkGetBufferDeviceAddressKHR");

  pvkCreateRayTracingPipelinesKHR =
      (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(
          deviceHandle, "vkCreateRayTracingPipelinesKHR");

  
  pvkGetAccelerationStructureBuildSizesKHR =
      (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(
          deviceHandle, "vkGetAccelerationStructureBuildSizesKHR");

  pvkCreateAccelerationStructureKHR =
      (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(
          deviceHandle, "vkCreateAccelerationStructureKHR");

  pvkDestroyAccelerationStructureKHR =
      (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(
          deviceHandle, "vkDestroyAccelerationStructureKHR");

  pvkGetAccelerationStructureDeviceAddressKHR =
      (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(
          deviceHandle, "vkGetAccelerationStructureDeviceAddressKHR");

  pvkCmdBuildAccelerationStructuresKHR =
      (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(
          deviceHandle, "vkCmdBuildAccelerationStructuresKHR");

  pvkGetRayTracingShaderGroupHandlesKHR =
      (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(
          deviceHandle, "vkGetRayTracingShaderGroupHandlesKHR");

  pvkCmdTraceRaysKHR =
      (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(deviceHandle,
                                                 "vkCmdTraceRaysKHR");

  VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
      .pNext = NULL,
      .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
      .deviceMask = 0};

  // =========================================================================
  // Command Pool

  VkCommandPoolCreateInfo commandPoolCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext = NULL,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = queueFamilyIndex};

  VkCommandPool commandPoolHandle = VK_NULL_HANDLE;
  result = vkCreateCommandPool(deviceHandle, &commandPoolCreateInfo, NULL,
                               &commandPoolHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateCommandPool");
  }

  // =========================================================================
  // Command Buffers

  VkCommandBufferAllocateInfo commandBufferAllocateInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = NULL,
      .commandPool = commandPoolHandle,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 16};

  std::vector<VkCommandBuffer> commandBufferHandleList =
      std::vector<VkCommandBuffer>(16, VK_NULL_HANDLE);

  result = vkAllocateCommandBuffers(deviceHandle, &commandBufferAllocateInfo,
                                    commandBufferHandleList.data());

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkAllocateCommandBuffers");
  }

  // =========================================================================
  // Surface Features

  VkSurfaceCapabilitiesKHR surfaceCapabilities;
  result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
      activePhysicalDeviceHandle, surfaceHandle, &surfaceCapabilities);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result,
                            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
  }

  uint32_t surfaceFormatCount = 0;
  result = vkGetPhysicalDeviceSurfaceFormatsKHR(
      activePhysicalDeviceHandle, surfaceHandle, &surfaceFormatCount, NULL);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkGetPhysicalDeviceSurfaceFormatsKHR");
  }

  std::vector<VkSurfaceFormatKHR> surfaceFormatList(surfaceFormatCount);
  result = vkGetPhysicalDeviceSurfaceFormatsKHR(
      activePhysicalDeviceHandle, surfaceHandle, &surfaceFormatCount,
      surfaceFormatList.data());

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkGetPhysicalDeviceSurfaceFormatsKHR");
  }

  uint32_t presentModeCount = 0;
  result = vkGetPhysicalDeviceSurfacePresentModesKHR(
      activePhysicalDeviceHandle, surfaceHandle, &presentModeCount, NULL);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result,
                            "vkGetPhysicalDeviceSurfacePresentModesKHR");
  }

  std::vector<VkPresentModeKHR> presentModeList(presentModeCount);
  result = vkGetPhysicalDeviceSurfacePresentModesKHR(
      activePhysicalDeviceHandle, surfaceHandle, &presentModeCount,
      presentModeList.data());

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result,
                            "vkGetPhysicalDeviceSurfacePresentModesKHR");
  }

  // =========================================================================
  // Swapchain

  VkSwapchainCreateInfoKHR swapchainCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .pNext = NULL,
      .flags = 0,
      .surface = surfaceHandle,
      .minImageCount = surfaceCapabilities.minImageCount + 1,
      .imageFormat = surfaceFormatList[0].format,
      .imageColorSpace = surfaceFormatList[0].colorSpace,
      .imageExtent = surfaceCapabilities.currentExtent,
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 1,
      .pQueueFamilyIndices = &queueFamilyIndex,
      .preTransform = surfaceCapabilities.currentTransform,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = presentModeList[0],
      .clipped = VK_TRUE,
      .oldSwapchain = VK_NULL_HANDLE};

  VkSwapchainKHR swapchainHandle = VK_NULL_HANDLE;
  result = vkCreateSwapchainKHR(deviceHandle, &swapchainCreateInfo, NULL,
                                &swapchainHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateSwapchainKHR");
  }

  // =========================================================================
  // Swapchain Images

  uint32_t swapchainImageCount = 0;
  result = vkGetSwapchainImagesKHR(deviceHandle, swapchainHandle,
                                   &swapchainImageCount, NULL);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkGetSwapchainImagesKHR");
  }

  std::vector<VkImage> swapchainImageHandleList(swapchainImageCount);
  result = vkGetSwapchainImagesKHR(deviceHandle, swapchainHandle,
                                   &swapchainImageCount,
                                   swapchainImageHandleList.data());

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkGetSwapchainImagesKHR");
  }

  std::vector<VkImageView> swapchainImageViewHandleList(swapchainImageCount,
                                                        VK_NULL_HANDLE);

  for (uint32_t x = 0; x < swapchainImageCount; x++) {
    VkImageViewCreateInfo imageViewCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .image = swapchainImageHandleList[x],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = surfaceFormatList[0].format,
        .components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY},
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

    result = vkCreateImageView(deviceHandle, &imageViewCreateInfo, NULL,
                               &swapchainImageViewHandleList[x]);

    if (result != VK_SUCCESS) {
      throwExceptionVulkanAPI(result, "vkCreateImageView");
    }
  }

  // =========================================================================
  // Descriptor Pool

  std::vector<VkDescriptorPoolSize> descriptorPoolSizeList = {
      {.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
       .descriptorCount = 1},
      {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1},
      {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 4},
      {.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1}};

  VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .pNext = NULL,
      .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
      .maxSets = 2,
      .poolSizeCount = (uint32_t)descriptorPoolSizeList.size(),
      .pPoolSizes = descriptorPoolSizeList.data()};

  VkDescriptorPool descriptorPoolHandle = VK_NULL_HANDLE;
  result = vkCreateDescriptorPool(deviceHandle, &descriptorPoolCreateInfo, NULL,
                                  &descriptorPoolHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateDescriptorPool");
  }

  // =========================================================================
  // Descriptor Set Layout

  std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindingList = {
      {.binding = 0,
       .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
       .descriptorCount = 1,
       .stageFlags =
           VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
       .pImmutableSamplers = NULL},
      {.binding = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
       .descriptorCount = 1,
       .stageFlags =
           VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
       .pImmutableSamplers = NULL},
      {.binding = 2,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       .descriptorCount = 1,
       .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
       .pImmutableSamplers = NULL},
      {.binding = 3,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       .descriptorCount = 1,
       .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
       .pImmutableSamplers = NULL},
      {.binding = 4,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
       .descriptorCount = 1,
       .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
       .pImmutableSamplers = NULL}};

  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .bindingCount = (uint32_t)descriptorSetLayoutBindingList.size(),
      .pBindings = descriptorSetLayoutBindingList.data()};

  VkDescriptorSetLayout descriptorSetLayoutHandle = VK_NULL_HANDLE;
  result =
      vkCreateDescriptorSetLayout(deviceHandle, &descriptorSetLayoutCreateInfo,
                                  NULL, &descriptorSetLayoutHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateDescriptorSetLayout");
  }

  // =========================================================================
  // Material Descriptor Set Layout

  std::vector<VkDescriptorSetLayoutBinding>
      materialDescriptorSetLayoutBindingList = {
          {.binding = 0,
           .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           .descriptorCount = 1,
           .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
           .pImmutableSamplers = NULL},
          {.binding = 1,
           .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           .descriptorCount = 1,
           .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
           .pImmutableSamplers = NULL}};

  VkDescriptorSetLayoutCreateInfo materialDescriptorSetLayoutCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .bindingCount = (uint32_t)materialDescriptorSetLayoutBindingList.size(),
      .pBindings = materialDescriptorSetLayoutBindingList.data()};

  VkDescriptorSetLayout materialDescriptorSetLayoutHandle = VK_NULL_HANDLE;
  result = vkCreateDescriptorSetLayout(
      deviceHandle, &materialDescriptorSetLayoutCreateInfo, NULL,
      &materialDescriptorSetLayoutHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateDescriptorSetLayout");
  }

  // =========================================================================
  // Allocate Descriptor Sets

  std::vector<VkDescriptorSetLayout> descriptorSetLayoutHandleList = {
      descriptorSetLayoutHandle, materialDescriptorSetLayoutHandle};

  VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .pNext = NULL,
      .descriptorPool = descriptorPoolHandle,
      .descriptorSetCount = (uint32_t)descriptorSetLayoutHandleList.size(),
      .pSetLayouts = descriptorSetLayoutHandleList.data()};

  std::vector<VkDescriptorSet> descriptorSetHandleList =
      std::vector<VkDescriptorSet>(2, VK_NULL_HANDLE);

  result = vkAllocateDescriptorSets(deviceHandle, &descriptorSetAllocateInfo,
                                    descriptorSetHandleList.data());

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkAllocateDescriptorSets");
  }

  // =========================================================================
  // Pipeline Layout

  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .setLayoutCount = (uint32_t)descriptorSetLayoutHandleList.size(),
      .pSetLayouts = descriptorSetLayoutHandleList.data(),
      .pushConstantRangeCount = 0,
      .pPushConstantRanges = NULL};

  VkPipelineLayout pipelineLayoutHandle = VK_NULL_HANDLE;
  result = vkCreatePipelineLayout(deviceHandle, &pipelineLayoutCreateInfo, NULL,
                                  &pipelineLayoutHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreatePipelineLayout");
  }

  // =========================================================================
  // Ray Closest Hit Shader Module

  std::ifstream rayClosestHitFile("shaders/shader.rchit.spv",
                                  std::ios::binary | std::ios::ate);
  std::streamsize rayClosestHitFileSize = rayClosestHitFile.tellg();
  rayClosestHitFile.seekg(0, std::ios::beg);
  std::vector<uint32_t> rayClosestHitShaderSource(rayClosestHitFileSize /
                                                  sizeof(uint32_t));
  rayClosestHitFile.read((char *)rayClosestHitShaderSource.data(),
                         rayClosestHitFileSize);
  rayClosestHitFile.close();

  VkShaderModuleCreateInfo rayClosestHitShaderModuleCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .codeSize = (uint32_t)rayClosestHitShaderSource.size() * sizeof(uint32_t),
      .pCode = rayClosestHitShaderSource.data()};

  VkShaderModule rayClosestHitShaderModuleHandle = VK_NULL_HANDLE;
  result =
      vkCreateShaderModule(deviceHandle, &rayClosestHitShaderModuleCreateInfo,
                           NULL, &rayClosestHitShaderModuleHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateShaderModule");
  }

  // =========================================================================
  // Ray Generate Shader Module

  std::ifstream rayGenerateFile("shaders/shader.rgen.spv",
                                std::ios::binary | std::ios::ate);
  std::streamsize rayGenerateFileSize = rayGenerateFile.tellg();
  rayGenerateFile.seekg(0, std::ios::beg);
  std::vector<uint32_t> rayGenerateShaderSource(rayGenerateFileSize /
                                                sizeof(uint32_t));
  rayGenerateFile.read((char *)rayGenerateShaderSource.data(),
                       rayGenerateFileSize);
  rayGenerateFile.close();

  VkShaderModuleCreateInfo rayGenerateShaderModuleCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .codeSize = (uint32_t)rayGenerateShaderSource.size() * sizeof(uint32_t),
      .pCode = rayGenerateShaderSource.data()};

  VkShaderModule rayGenerateShaderModuleHandle = VK_NULL_HANDLE;
  result =
      vkCreateShaderModule(deviceHandle, &rayGenerateShaderModuleCreateInfo,
                           NULL, &rayGenerateShaderModuleHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateShaderModule");
  }

  // =========================================================================
  // Ray Miss Shader Module

  std::ifstream rayMissFile("shaders/shader.rmiss.spv",
                            std::ios::binary | std::ios::ate);
  std::streamsize rayMissFileSize = rayMissFile.tellg();
  rayMissFile.seekg(0, std::ios::beg);
  std::vector<uint32_t> rayMissShaderSource(rayMissFileSize / sizeof(uint32_t));
  rayMissFile.read((char *)rayMissShaderSource.data(), rayMissFileSize);
  rayMissFile.close();

  VkShaderModuleCreateInfo rayMissShaderModuleCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .codeSize = (uint32_t)rayMissShaderSource.size() * sizeof(uint32_t),
      .pCode = rayMissShaderSource.data()};

  VkShaderModule rayMissShaderModuleHandle = VK_NULL_HANDLE;
  result = vkCreateShaderModule(deviceHandle, &rayMissShaderModuleCreateInfo,
                                NULL, &rayMissShaderModuleHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateShaderModule");
  }

  // =========================================================================
  // Ray Miss Shader Module (Shadow)

  std::ifstream rayMissShadowFile("shaders/shader_shadow.rmiss.spv",
                                  std::ios::binary | std::ios::ate);
  std::streamsize rayMissShadowFileSize = rayMissShadowFile.tellg();
  rayMissShadowFile.seekg(0, std::ios::beg);
  std::vector<uint32_t> rayMissShadowShaderSource(rayMissShadowFileSize /
                                                  sizeof(uint32_t));
  rayMissShadowFile.read((char *)rayMissShadowShaderSource.data(),
                         rayMissShadowFileSize);
  rayMissShadowFile.close();

  VkShaderModuleCreateInfo rayMissShadowShaderModuleCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .codeSize = (uint32_t)rayMissShadowShaderSource.size() * sizeof(uint32_t),
      .pCode = rayMissShadowShaderSource.data()};

  VkShaderModule rayMissShadowShaderModuleHandle = VK_NULL_HANDLE;
  result =
      vkCreateShaderModule(deviceHandle, &rayMissShadowShaderModuleCreateInfo,
                           NULL, &rayMissShadowShaderModuleHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateShaderModule");
  }

  // =========================================================================
  // Ray Tracing Pipeline

  std::vector<VkPipelineShaderStageCreateInfo>
      pipelineShaderStageCreateInfoList = {
          {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
           .pNext = NULL,
           .flags = 0,
           .stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
           .module = rayClosestHitShaderModuleHandle,
           .pName = "main",
           .pSpecializationInfo = NULL},
          {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
           .pNext = NULL,
           .flags = 0,
           .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
           .module = rayGenerateShaderModuleHandle,
           .pName = "main",
           .pSpecializationInfo = NULL},
          {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
           .pNext = NULL,
           .flags = 0,
           .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
           .module = rayMissShaderModuleHandle,
           .pName = "main",
           .pSpecializationInfo = NULL},
          {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
           .pNext = NULL,
           .flags = 0,
           .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
           .module = rayMissShadowShaderModuleHandle,
           .pName = "main",
           .pSpecializationInfo = NULL}};

  std::vector<VkRayTracingShaderGroupCreateInfoKHR>
      rayTracingShaderGroupCreateInfoList = {
          {.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
           .pNext = NULL,
           .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
           .generalShader = VK_SHADER_UNUSED_KHR,
           .closestHitShader = 0,
           .anyHitShader = VK_SHADER_UNUSED_KHR,
           .intersectionShader = VK_SHADER_UNUSED_KHR,
           .pShaderGroupCaptureReplayHandle = NULL},
          {.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
           .pNext = NULL,
           .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
           .generalShader = 1,
           .closestHitShader = VK_SHADER_UNUSED_KHR,
           .anyHitShader = VK_SHADER_UNUSED_KHR,
           .intersectionShader = VK_SHADER_UNUSED_KHR,
           .pShaderGroupCaptureReplayHandle = NULL},
          {.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
           .pNext = NULL,
           .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
           .generalShader = 2,
           .closestHitShader = VK_SHADER_UNUSED_KHR,
           .anyHitShader = VK_SHADER_UNUSED_KHR,
           .intersectionShader = VK_SHADER_UNUSED_KHR,
           .pShaderGroupCaptureReplayHandle = NULL},
          {.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
           .pNext = NULL,
           .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
           .generalShader = 3,
           .closestHitShader = VK_SHADER_UNUSED_KHR,
           .anyHitShader = VK_SHADER_UNUSED_KHR,
           .intersectionShader = VK_SHADER_UNUSED_KHR,
           .pShaderGroupCaptureReplayHandle = NULL}};

  VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
      .pNext = NULL,
      .flags = 0,
      .stageCount = 4,
      .pStages = pipelineShaderStageCreateInfoList.data(),
      .groupCount = 4,
      .pGroups = rayTracingShaderGroupCreateInfoList.data(),
      .maxPipelineRayRecursionDepth = 1,
      .pLibraryInfo = NULL,
      .pLibraryInterface = NULL,
      .pDynamicState = NULL,
      .layout = pipelineLayoutHandle,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex = 0};

  VkPipeline rayTracingPipelineHandle = VK_NULL_HANDLE;
  result = pvkCreateRayTracingPipelinesKHR(
      deviceHandle, VK_NULL_HANDLE, VK_NULL_HANDLE, 1,
      &rayTracingPipelineCreateInfo, NULL, &rayTracingPipelineHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateRayTracingPipelinesKHR");
  }

  // =========================================================================
  // OBJ Model

  tinyobj::ObjReaderConfig reader_config;
  tinyobj::ObjReader reader;

  std::vector<tinyobj::attrib_t> attrib;
  std::vector<std::vector<tinyobj::shape_t>> shapes;
  std::vector<std::vector<tinyobj::material_t>> materials;
  

  std::vector<const char*> fileNames = {
    "resources/cube_scene.obj"
  };

  uint32_t objectCount = fileNames.size();

  for(const char* fileName: fileNames){
    parseFile(reader_config, reader, fileName);

    attrib.push_back(reader.GetAttrib());

    const std::vector<tinyobj::shape_t>& readShapes = reader.GetShapes();
    shapes.push_back(readShapes);
     
    const std::vector<tinyobj::material_t>& readMaterials = reader.GetMaterials();
    materials.push_back(readMaterials);
  }

  assert(attrib.size() == objectCount);
  assert(shapes.size() == objectCount);
  assert(materials.size() == objectCount);

  std::vector<uint32_t> primitiveCount(objectCount, 0);

  std::vector<std::vector<uint32_t>> indexList;
  for(int i = 0; i < objectCount; i++){
    std::vector<uint32_t> currIndexList;

    for (tinyobj::shape_t shape : shapes[i]) {
      primitiveCount[i] += shape.mesh.num_face_vertices.size();
      
      for (tinyobj::index_t index : shape.mesh.indices) {
        currIndexList.push_back(index.vertex_index);
      }
    }
    
    indexList.push_back(currIndexList);
  }
 

  // =========================================================================
  // Vertex Buffer

  std::vector<VkBuffer> vertexBufferHandle(objectCount, VK_NULL_HANDLE);
  std::vector<VkDeviceMemory> vertexDeviceMemoryHandle(objectCount, VK_NULL_HANDLE);
  std::vector<VkDeviceAddress> vertexBufferDeviceAddress(objectCount);
  
  for(int i = 0; i < objectCount; i++){
      createVertexBuffer(vertexBufferHandle[i], 
        attrib[i], 
        queueFamilyIndex, 
        memoryAllocateFlagsInfo,
        vertexDeviceMemoryHandle[i],
        vertexBufferDeviceAddress[i]);
  }


  // =========================================================================
  // Index Buffer

  std::vector<VkBuffer> indexBufferHandle(objectCount, VK_NULL_HANDLE);
  std::vector<VkDeviceMemory> indexDeviceMemoryHandle(objectCount, VK_NULL_HANDLE);
  std::vector<VkDeviceAddress> indexBufferDeviceAddress(objectCount);

  for(int i = 0; i < objectCount; i++){
    createIndexBuffer(indexBufferHandle[i], 
      indexList[i], 
      queueFamilyIndex, 
      memoryAllocateFlagsInfo,
      indexDeviceMemoryHandle[i],
      indexBufferDeviceAddress[i]
    );
  }

  // =========================================================================
  // Bottom Level Acceleration Structure
  
  std::vector<VkAccelerationStructureGeometryKHR> bottomLevelAccelerationStructureGeometry(objectCount);

  for(int i = 0; i < objectCount; i++){
    createBLASGeometry(bottomLevelAccelerationStructureGeometry[i],
      vertexBufferDeviceAddress[i],
      indexBufferDeviceAddress[i],
      attrib[i]);
  }

  //Create offset info
  std::vector<VkAccelerationStructureBuildRangeInfoKHR> bottomLevelAccelerationStructureBuildRangeInfo(objectCount);

  for(int i = 0; i < objectCount; i++){
    bottomLevelAccelerationStructureBuildRangeInfo[i] =  {.primitiveCount =
                                                         primitiveCount[i], 
                                                        .primitiveOffset = 0,
                                                        .firstVertex = 0,
                                                        .transformOffset = 0};
  }

  std::vector<VkAccelerationStructureKHR> bottomLevelAccelerationStructureHandle(objectCount);
  std::vector<VkBuffer> bottomLevelAccelerationStructureBufferHandle(objectCount);
  std::vector<VkDeviceMemory> bottomLevelAccelerationStructureDeviceMemoryHandle(objectCount);
  std::vector<VkAccelerationStructureBuildSizesInfoKHR> bottomLevelAccelerationStructureBuildSizesInfo(objectCount);
  std::vector<VkAccelerationStructureBuildGeometryInfoKHR>  bottomLevelAccelerationStructureBuildGeometryInfo(objectCount);


  for(int i = 0; i < objectCount; i++){
    createBLAS(bottomLevelAccelerationStructureHandle[i],
      bottomLevelAccelerationStructureGeometry[i],
      primitiveCount[i],
      queueFamilyIndex,
      bottomLevelAccelerationStructureBufferHandle[i],
      bottomLevelAccelerationStructureDeviceMemoryHandle[i],
      bottomLevelAccelerationStructureBuildSizesInfo[i],
      bottomLevelAccelerationStructureBuildGeometryInfo[i]);
  }
 
  // =========================================================================
  // Build Bottom Level Acceleration Structure
  std::vector<VkBuffer> bottomLevelAccelerationStructureScratchBufferHandle(objectCount);
  std::vector<VkDeviceAddress> bottomLevelAccelerationStructureDeviceAddress(objectCount);
  std::vector<VkDeviceMemory> bottomLevelAccelerationStructureDeviceScratchMemoryHandle(objectCount);

  for(int i = 0; i < objectCount; i++){
    createBLASScratchBuffer(bottomLevelAccelerationStructureScratchBufferHandle[i],
      bottomLevelAccelerationStructureHandle[i],
      bottomLevelAccelerationStructureDeviceAddress[i],
      bottomLevelAccelerationStructureBuildSizesInfo[i],
      queueFamilyIndex,
      memoryAllocateFlagsInfo,
      bottomLevelAccelerationStructureDeviceScratchMemoryHandle[i],
      bottomLevelAccelerationStructureBuildGeometryInfo[i]);
  }

  VkFenceCreateInfo bottomLevelAccelerationStructureBuildFenceCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = NULL, .flags = 0};

  VkFence bottomLevelAccelerationStructureBuildFenceHandle = VK_NULL_HANDLE;
  result = vkCreateFence(
      deviceHandle, &bottomLevelAccelerationStructureBuildFenceCreateInfo, NULL,
      &bottomLevelAccelerationStructureBuildFenceHandle);
  
  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateFence");
  }

  for(int i = 0; i < objectCount; i++){
    buildBLAS(commandBufferHandleList.back(),
      bottomLevelAccelerationStructureBuildRangeInfo[i],
      bottomLevelAccelerationStructureBuildGeometryInfo[i],
      bottomLevelAccelerationStructureBuildFenceHandle,
      deviceHandle,
      queueHandle);
  }

  // =========================================================================
  // Top Level Acceleration Structure

   //TODO add matrix vector for each instance
  VkTransformMatrixKHR transformMatrix ={
    .matrix = 
     {{1.0, 0.0, 0.0, 0.0},
      {0.0, 1.0, 0.0, 0.0},
      {0.0, 0.0, 1.0, 0.0}}
  };

  std::vector<VkAccelerationStructureKHR> topLevelAccelerationStructureHandle(objectCount);
  std::vector<VkBuffer> topLevelAccelerationStructureBufferHandle(objectCount);
  std::vector<VkDeviceMemory> topLevelAccelerationStructureDeviceMemoryHandle(objectCount);

  for(int i = 0; i < objectCount; i++){
   createTLAS(topLevelAccelerationStructureHandle[i],
      bottomLevelAccelerationStructureDeviceAddress[i],
      transformMatrix,
      i,
      queueFamilyIndex,
      memoryAllocateFlagsInfo,
      topLevelAccelerationStructureBufferHandle[i],
      topLevelAccelerationStructureDeviceMemoryHandle[i],
      commandBufferHandleList.back(),
      queueHandle,
      false);
  }
    
  // =========================================================================
  // Build Top Level Acceleration Structure

  // Multiple TLAS (one TLAS multiple instances in NV)

  //create and build in the same function
  
  // =========================================================================
  // Uniform Buffer

  struct UniformStructure {
    float cameraPosition[4] = {0, 0, 0, 1};
    float cameraRight[4] = {1, 0, 0, 1};
    float cameraUp[4] = {0, 1, 0, 1};
    float cameraForward[4] = {0, 0, 1, 1};

    uint32_t frameCount = 0;
  } uniformStructure;

  VkBuffer uniformBufferHandle = VK_NULL_HANDLE;
  createBuffer(uniformBufferHandle,
    sizeof(UniformStructure),
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    queueFamilyIndex);

  VkDeviceMemory uniformDeviceMemoryHandle = VK_NULL_HANDLE;
  allocAndBind(uniformDeviceMemoryHandle,
    &memoryAllocateFlagsInfo,
    uniformBufferHandle,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  
  copyData(uniformDeviceMemoryHandle,
    (void *) &uniformStructure,
    sizeof(UniformStructure));

  // =========================================================================
  // Ray Trace Image

  VkImageCreateInfo rayTraceImageCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = surfaceFormatList[0].format,
      .extent = {.width = surfaceCapabilities.currentExtent.width,
                 .height = surfaceCapabilities.currentExtent.height,
                 .depth = 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 1,
      .pQueueFamilyIndices = &queueFamilyIndex,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};

  VkImage rayTraceImageHandle = VK_NULL_HANDLE;
  result = vkCreateImage(deviceHandle, &rayTraceImageCreateInfo, NULL,
                         &rayTraceImageHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateImage");
  }

  VkMemoryRequirements rayTraceImageMemoryRequirements;
  vkGetImageMemoryRequirements(deviceHandle, rayTraceImageHandle,
                               &rayTraceImageMemoryRequirements);

  uint32_t rayTraceImageMemoryTypeIndex = getMemoryIndex(rayTraceImageMemoryRequirements,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  
  VkMemoryAllocateInfo rayTraceImageMemoryAllocateInfo = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = NULL,
      .allocationSize = rayTraceImageMemoryRequirements.size,
      .memoryTypeIndex = rayTraceImageMemoryTypeIndex};

  VkDeviceMemory rayTraceImageDeviceMemoryHandle = VK_NULL_HANDLE;
  result = vkAllocateMemory(deviceHandle, &rayTraceImageMemoryAllocateInfo,
                            NULL, &rayTraceImageDeviceMemoryHandle);
  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkAllocateMemory");
  }

  result = vkBindImageMemory(deviceHandle, rayTraceImageHandle,
                             rayTraceImageDeviceMemoryHandle, 0);
  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkBindImageMemory");
  }

  VkImageViewCreateInfo rayTraceImageViewCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .image = rayTraceImageHandle,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = surfaceFormatList[0].format,
      .components = {.r = VK_COMPONENT_SWIZZLE_IDENTITY,
                     .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                     .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                     .a = VK_COMPONENT_SWIZZLE_IDENTITY},
      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .baseMipLevel = 0,
                           .levelCount = 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1}};

  VkImageView rayTraceImageViewHandle = VK_NULL_HANDLE;
  result = vkCreateImageView(deviceHandle, &rayTraceImageViewCreateInfo, NULL,
                             &rayTraceImageViewHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateImageView");
  }

  // =========================================================================
  // Ray Trace Image Barrier
  // (VK_IMAGE_LAYOUT_UNDEFINED -> VK_IMAGE_LAYOUT_GENERAL)

  VkCommandBufferBeginInfo rayTraceImageBarrierCommandBufferBeginInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = NULL,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      .pInheritanceInfo = NULL};

  result = vkBeginCommandBuffer(commandBufferHandleList.back(),
                                &rayTraceImageBarrierCommandBufferBeginInfo);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkBeginCommandBuffer");
  }

  VkImageMemoryBarrier rayTraceGeneralMemoryBarrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext = NULL,
      .srcAccessMask = 0,
      .dstAccessMask = 0,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex = queueFamilyIndex,
      .dstQueueFamilyIndex = queueFamilyIndex,
      .image = rayTraceImageHandle,
      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .baseMipLevel = 0,
                           .levelCount = 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1}};

  vkCmdPipelineBarrier(commandBufferHandleList.back(),
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL,
                       1, &rayTraceGeneralMemoryBarrier);

  result = vkEndCommandBuffer(commandBufferHandleList.back());

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkEndCommandBuffer");
  }

  VkSubmitInfo rayTraceImageBarrierAccelerationStructureBuildSubmitInfo = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = NULL,
      .waitSemaphoreCount = 0,
      .pWaitSemaphores = NULL,
      .pWaitDstStageMask = NULL,
      .commandBufferCount = 1,
      .pCommandBuffers = &commandBufferHandleList.back(),
      .signalSemaphoreCount = 0,
      .pSignalSemaphores = NULL};

  VkFenceCreateInfo
      rayTraceImageBarrierAccelerationStructureBuildFenceCreateInfo = {
          .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
          .pNext = NULL,
          .flags = 0};

  VkFence rayTraceImageBarrierAccelerationStructureBuildFenceHandle =
      VK_NULL_HANDLE;
  result = vkCreateFence(
      deviceHandle,
      &rayTraceImageBarrierAccelerationStructureBuildFenceCreateInfo, NULL,
      &rayTraceImageBarrierAccelerationStructureBuildFenceHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkCreateFence");
  }

  result = vkQueueSubmit(
      queueHandle, 1, &rayTraceImageBarrierAccelerationStructureBuildSubmitInfo,
      rayTraceImageBarrierAccelerationStructureBuildFenceHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkQueueSubmit");
  }

  result = vkWaitForFences(
      deviceHandle, 1,
      &rayTraceImageBarrierAccelerationStructureBuildFenceHandle, true,
      UINT32_MAX);

  if (result != VK_SUCCESS && result != VK_TIMEOUT) {
    throwExceptionVulkanAPI(result, "vkWaitForFences");
  }

  // =========================================================================
  // Update Descriptor Set

  VkWriteDescriptorSetAccelerationStructureKHR
      accelerationStructureDescriptorInfo = {
          .sType =
              VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
          .pNext = NULL,
          .accelerationStructureCount = 1,
          .pAccelerationStructures = &topLevelAccelerationStructureHandle[0]};

  VkDescriptorBufferInfo uniformDescriptorInfo = {
      .buffer = uniformBufferHandle, .offset = 0, .range = VK_WHOLE_SIZE};

  VkDescriptorBufferInfo indexDescriptorInfo = {
      .buffer = indexBufferHandle[0], .offset = 0, .range = VK_WHOLE_SIZE}; //TODO loop

  VkDescriptorBufferInfo vertexDescriptorInfo = {
      .buffer = vertexBufferHandle[0], .offset = 0, .range = VK_WHOLE_SIZE}; //TODO loop

  VkDescriptorImageInfo rayTraceImageDescriptorInfo = {
      .sampler = VK_NULL_HANDLE,
      .imageView = rayTraceImageViewHandle,
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL};

  std::vector<VkWriteDescriptorSet> writeDescriptorSetList = {
      {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
       .pNext = &accelerationStructureDescriptorInfo,
       .dstSet = descriptorSetHandleList[0],
       .dstBinding = 0,
       .dstArrayElement = 0,
       .descriptorCount = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
       .pImageInfo = NULL,
       .pBufferInfo = NULL,
       .pTexelBufferView = NULL},
      {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
       .pNext = NULL,
       .dstSet = descriptorSetHandleList[0],
       .dstBinding = 1,
       .dstArrayElement = 0,
       .descriptorCount = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
       .pImageInfo = NULL,
       .pBufferInfo = &uniformDescriptorInfo,
       .pTexelBufferView = NULL},
      {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
       .pNext = NULL,
       .dstSet = descriptorSetHandleList[0],
       .dstBinding = 2,
       .dstArrayElement = 0,
       .descriptorCount = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       .pImageInfo = NULL,
       .pBufferInfo = &indexDescriptorInfo,
       .pTexelBufferView = NULL},
      {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
       .pNext = NULL,
       .dstSet = descriptorSetHandleList[0],
       .dstBinding = 3,
       .dstArrayElement = 0,
       .descriptorCount = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       .pImageInfo = NULL,
       .pBufferInfo = &vertexDescriptorInfo,
       .pTexelBufferView = NULL},
      {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
       .pNext = NULL,
       .dstSet = descriptorSetHandleList[0],
       .dstBinding = 4,
       .dstArrayElement = 0,
       .descriptorCount = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
       .pImageInfo = &rayTraceImageDescriptorInfo,
       .pBufferInfo = NULL,
       .pTexelBufferView = NULL}};

  vkUpdateDescriptorSets(deviceHandle, writeDescriptorSetList.size(),
                         writeDescriptorSetList.data(), 0, NULL);

  // =========================================================================
  // Material Index Buffer

  std::vector<std::vector<uint32_t>> materialIndexList(objectCount); //TODO loop

  for(int i = 0; i < objectCount; i++){
    for (tinyobj::shape_t shape : shapes[i]) {
      for (int index : shape.mesh.material_ids) {
        materialIndexList[i].push_back(index);
      }
    }
  }

  VkBuffer materialIndexBufferHandle = VK_NULL_HANDLE;
  createBuffer(materialIndexBufferHandle,
    sizeof(uint32_t) * materialIndexList[0].size(),
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    queueFamilyIndex);

  VkDeviceMemory materialIndexDeviceMemoryHandle = VK_NULL_HANDLE;
  allocAndBind(materialIndexDeviceMemoryHandle,
    &memoryAllocateFlagsInfo,
    materialIndexBufferHandle,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  
  copyData(materialIndexDeviceMemoryHandle,
    (void *) materialIndexList[0].data(),
    sizeof(uint32_t) * materialIndexList[0].size());

  // =========================================================================
  // Material Buffer

  struct Material {
    float ambient[4] = {0, 0, 0, 0};
    float diffuse[4] = {0, 0, 0, 0};
    float specular[4] = {0, 0, 0, 0};
    float emission[4] = {0, 0, 0, 0};
  };

  std::vector<Material> materialList(materials[0].size());
  for (uint32_t x = 0; x < materials[0].size(); x++) { //TODO loop
    memcpy(materialList[x].ambient, materials[0][x].ambient, sizeof(float) * 3);
    memcpy(materialList[x].diffuse, materials[0][x].diffuse, sizeof(float) * 3);
    memcpy(materialList[x].specular, materials[0][x].specular, sizeof(float) * 3);
    memcpy(materialList[x].emission, materials[0][x].emission, sizeof(float) * 3);
  }

  VkBuffer materialBufferHandle = VK_NULL_HANDLE;
  createBuffer(materialBufferHandle,
    sizeof(Material) * materialList.size(),
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    queueFamilyIndex);

  VkDeviceMemory materialDeviceMemoryHandle = VK_NULL_HANDLE;
  allocAndBind(materialDeviceMemoryHandle,
    &memoryAllocateFlagsInfo,
    materialBufferHandle,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  
  copyData(materialDeviceMemoryHandle,
    (void *)  materialList.data(),
    sizeof(Material) * materialList.size());

  // =========================================================================
  // Update Material Descriptor Set

  VkDescriptorBufferInfo materialIndexDescriptorInfo = {
      .buffer = materialIndexBufferHandle, .offset = 0, .range = VK_WHOLE_SIZE};

  VkDescriptorBufferInfo materialDescriptorInfo = {
      .buffer = materialBufferHandle, .offset = 0, .range = VK_WHOLE_SIZE};

  std::vector<VkWriteDescriptorSet> materialWriteDescriptorSetList = {
      {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
       .pNext = NULL,
       .dstSet = descriptorSetHandleList[1],
       .dstBinding = 0,
       .dstArrayElement = 0,
       .descriptorCount = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       .pImageInfo = NULL,
       .pBufferInfo = &materialIndexDescriptorInfo,
       .pTexelBufferView = NULL},
      {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
       .pNext = NULL,
       .dstSet = descriptorSetHandleList[1],
       .dstBinding = 1,
       .dstArrayElement = 0,
       .descriptorCount = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       .pImageInfo = NULL,
       .pBufferInfo = &materialDescriptorInfo,
       .pTexelBufferView = NULL}};

  vkUpdateDescriptorSets(deviceHandle, materialWriteDescriptorSetList.size(),
                         materialWriteDescriptorSetList.data(), 0, NULL);

  // =========================================================================
  // Shader Binding Table

  VkDeviceSize shaderBindingTableSize =
      physicalDeviceRayTracingPipelineProperties.shaderGroupHandleSize * 4;

  VkBuffer shaderBindingTableBufferHandle = VK_NULL_HANDLE;
  createBuffer(shaderBindingTableBufferHandle,
    shaderBindingTableSize,
      VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    queueFamilyIndex);

  VkDeviceMemory shaderBindingTableDeviceMemoryHandle = VK_NULL_HANDLE;
  allocAndBind(shaderBindingTableDeviceMemoryHandle,
    &memoryAllocateFlagsInfo,
    shaderBindingTableBufferHandle,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

  char *shaderHandleBuffer = new char[shaderBindingTableSize];
  result = pvkGetRayTracingShaderGroupHandlesKHR(
      deviceHandle, rayTracingPipelineHandle, 0, 4, shaderBindingTableSize,
      shaderHandleBuffer);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkGetRayTracingShaderGroupHandlesKHR");
  }

  void *hostShaderBindingTableMemoryBuffer;
  result = vkMapMemory(deviceHandle, shaderBindingTableDeviceMemoryHandle, 0,
                       shaderBindingTableSize, 0,
                       &hostShaderBindingTableMemoryBuffer);

  for (uint32_t x = 0; x < 4; x++) {
    memcpy(hostShaderBindingTableMemoryBuffer,
           shaderHandleBuffer + x * physicalDeviceRayTracingPipelineProperties
                                        .shaderGroupHandleSize,
           physicalDeviceRayTracingPipelineProperties.shaderGroupHandleSize);
    hostShaderBindingTableMemoryBuffer =
        (char *)hostShaderBindingTableMemoryBuffer +
        physicalDeviceRayTracingPipelineProperties.shaderGroupBaseAlignment;
  }

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkMapMemory");
  }

  vkUnmapMemory(deviceHandle, shaderBindingTableDeviceMemoryHandle);

  VkBufferDeviceAddressInfo shaderBindingTableBufferDeviceAddressInfo = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
      .pNext = NULL,
      .buffer = shaderBindingTableBufferHandle};

  VkDeviceAddress shaderBindingTableBufferDeviceAddress =
      pvkGetBufferDeviceAddressKHR(deviceHandle,
                                   &shaderBindingTableBufferDeviceAddressInfo);

  VkDeviceSize progSize =
      physicalDeviceRayTracingPipelineProperties.shaderGroupBaseAlignment;

  VkDeviceSize sbtSize = progSize * (VkDeviceSize)4;
  VkDeviceSize hitGroupOffset = 0u * progSize;
  VkDeviceSize rayGenOffset = 1u * progSize;
  VkDeviceSize missOffset = 2u * progSize;

  const VkStridedDeviceAddressRegionKHR rchitShaderBindingTable = {
      .deviceAddress = shaderBindingTableBufferDeviceAddress + 0u * progSize,
      .stride = progSize,
      .size = sbtSize * 1};

  const VkStridedDeviceAddressRegionKHR rgenShaderBindingTable = {
      .deviceAddress = shaderBindingTableBufferDeviceAddress + 1u * progSize,
      .stride = sbtSize,
      .size = sbtSize * 1};

  const VkStridedDeviceAddressRegionKHR rmissShaderBindingTable = {
      .deviceAddress = shaderBindingTableBufferDeviceAddress + 2u * progSize,
      .stride = progSize,
      .size = sbtSize * 2};

  const VkStridedDeviceAddressRegionKHR callableShaderBindingTable = {};

  // =========================================================================
  // Record Render Pass Command Buffers

  for (uint32_t x = 0; x < swapchainImageCount; x++) {
    VkCommandBufferBeginInfo renderCommandBufferBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = NULL};

    result = vkBeginCommandBuffer(commandBufferHandleList[x],
                                  &renderCommandBufferBeginInfo);

    if (result != VK_SUCCESS) {
      throwExceptionVulkanAPI(result, "vkBeginCommandBuffer");
    }

    vkCmdBindPipeline(commandBufferHandleList[x],
                      VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                      rayTracingPipelineHandle);

    vkCmdBindDescriptorSets(
        commandBufferHandleList[x], VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
        pipelineLayoutHandle, 0, (uint32_t)descriptorSetHandleList.size(),
        descriptorSetHandleList.data(), 0, NULL);

    pvkCmdTraceRaysKHR(commandBufferHandleList[x], &rgenShaderBindingTable,
                       &rmissShaderBindingTable, &rchitShaderBindingTable,
                       &callableShaderBindingTable,
                       surfaceCapabilities.currentExtent.width,
                       surfaceCapabilities.currentExtent.height, 1);

    VkImageMemoryBarrier swapchainCopyMemoryBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = NULL,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = queueFamilyIndex,
        .dstQueueFamilyIndex = queueFamilyIndex,
        .image = swapchainImageHandleList[x],
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1}};

    vkCmdPipelineBarrier(commandBufferHandleList[x],
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                         NULL, 1, &swapchainCopyMemoryBarrier);

    VkImageMemoryBarrier rayTraceCopyMemoryBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = NULL,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = queueFamilyIndex,
        .dstQueueFamilyIndex = queueFamilyIndex,
        .image = rayTraceImageHandle,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1}};

    vkCmdPipelineBarrier(commandBufferHandleList[x],
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                         NULL, 1, &rayTraceCopyMemoryBarrier);

    VkImageCopy imageCopy = {
        .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .mipLevel = 0,
                           .baseArrayLayer = 0,
                           .layerCount = 1},
        .srcOffset = {.x = 0, .y = 0, .z = 0},
        .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .mipLevel = 0,
                           .baseArrayLayer = 0,
                           .layerCount = 1},
        .dstOffset = {.x = 0, .y = 0, .z = 0},
        .extent = {.width = surfaceCapabilities.currentExtent.width,
                   .height = surfaceCapabilities.currentExtent.height,
                   .depth = 1}};

    vkCmdCopyImage(commandBufferHandleList[x], rayTraceImageHandle,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchainImageHandleList[x],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopy);

    VkImageMemoryBarrier swapchainPresentMemoryBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = NULL,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = queueFamilyIndex,
        .dstQueueFamilyIndex = queueFamilyIndex,
        .image = swapchainImageHandleList[x],
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1}};

    vkCmdPipelineBarrier(commandBufferHandleList[x],
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                         NULL, 1, &swapchainPresentMemoryBarrier);

    VkImageMemoryBarrier rayTraceWriteMemoryBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = NULL,
        .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = queueFamilyIndex,
        .dstQueueFamilyIndex = queueFamilyIndex,
        .image = rayTraceImageHandle,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1}};

    vkCmdPipelineBarrier(commandBufferHandleList[x],
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                         NULL, 1, &rayTraceWriteMemoryBarrier);

    result = vkEndCommandBuffer(commandBufferHandleList[x]);

    if (result != VK_SUCCESS) {
      throwExceptionVulkanAPI(result, "vkEndCommandBuffer");
    }
  }

  // =========================================================================
  // Fences, Semaphores

  std::vector<VkFence> imageAvailableFenceHandleList(swapchainImageCount,
                                                     VK_NULL_HANDLE);

  std::vector<VkSemaphore> acquireImageSemaphoreHandleList(swapchainImageCount,
                                                           VK_NULL_HANDLE);

  std::vector<VkSemaphore> writeImageSemaphoreHandleList(swapchainImageCount,
                                                         VK_NULL_HANDLE);

  for (uint32_t x = 0; x < swapchainImageCount; x++) {
    VkFenceCreateInfo imageAvailableFenceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT};

    result = vkCreateFence(deviceHandle, &imageAvailableFenceCreateInfo, NULL,
                           &imageAvailableFenceHandleList[x]);

    if (result != VK_SUCCESS) {
      throwExceptionVulkanAPI(result, "vkCreateFence");
    }

    VkSemaphoreCreateInfo acquireImageSemaphoreCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0};

    result = vkCreateSemaphore(deviceHandle, &acquireImageSemaphoreCreateInfo,
                               NULL, &acquireImageSemaphoreHandleList[x]);

    if (result != VK_SUCCESS) {
      throwExceptionVulkanAPI(result, "vkCreateSemaphore");
    }

    VkSemaphoreCreateInfo writeImageSemaphoreCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0};

    result = vkCreateSemaphore(deviceHandle, &writeImageSemaphoreCreateInfo,
                               NULL, &writeImageSemaphoreHandleList[x]);

    if (result != VK_SUCCESS) {
      throwExceptionVulkanAPI(result, "vkCreateSemaphore");
    }
  }

  // =========================================================================
  // Main Loop

  uint32_t currentFrame = 0;
  uint32_t currentImageIndex = 0;

  while (!glfwWindowShouldClose(windowPtr)) {
    glfwPollEvents();

    bool isCameraMoved = false;

    if (keyDownIndex[GLFW_KEY_W]) {
      cameraPosition[0] += cos(-cameraYaw - (M_PI / 2)) * 0.01f;
      cameraPosition[2] += sin(-cameraYaw - (M_PI / 2)) * 0.01f;
      isCameraMoved = true;
    }
    if (keyDownIndex[GLFW_KEY_S]) {
      cameraPosition[0] -= cos(-cameraYaw - (M_PI / 2)) * 0.01f;
      cameraPosition[2] -= sin(-cameraYaw - (M_PI / 2)) * 0.01f;
      isCameraMoved = true;
    }
    if (keyDownIndex[GLFW_KEY_A]) {
      cameraPosition[0] -= cos(-cameraYaw) * 0.01f;
      cameraPosition[2] -= sin(-cameraYaw) * 0.01f;
      isCameraMoved = true;
    }
    if (keyDownIndex[GLFW_KEY_D]) {
      cameraPosition[0] += cos(-cameraYaw) * 0.01f;
      cameraPosition[2] += sin(-cameraYaw) * 0.01f;
      isCameraMoved = true;
    }
    if (keyDownIndex[GLFW_KEY_SPACE]) {
      cameraPosition[1] += 0.01f;
      isCameraMoved = true;
    }
    if (keyDownIndex[GLFW_KEY_LEFT_CONTROL]) {
      cameraPosition[1] -= 0.01f;
      isCameraMoved = true;
    }
    if (keyDownIndex[GLFW_KEY_ESCAPE]) {
      glfwSetWindowShouldClose(windowPtr, GLFW_TRUE);
    }

    static double previousMousePositionX;
    static double previousMousePositionY;

    double xPos, yPos;
    glfwGetCursorPos(windowPtr, &xPos, &yPos);

    if (previousMousePositionX != xPos || previousMousePositionY != yPos) {
      double mouseDifferenceX = previousMousePositionX - xPos;
      double mouseDifferenceY = previousMousePositionY - yPos;

      cameraYaw += mouseDifferenceX * 0.0005f;

      previousMousePositionX = xPos;
      previousMousePositionY = yPos;

      isCameraMoved = 1;
    }

    if (isCameraMoved) {
      uniformStructure.cameraPosition[0] = cameraPosition[0];
      uniformStructure.cameraPosition[1] = cameraPosition[1];
      uniformStructure.cameraPosition[2] = cameraPosition[2];

      uniformStructure.cameraForward[0] =
          cosf(cameraPitch) * cosf(-cameraYaw - (M_PI / 2.0));
      uniformStructure.cameraForward[1] = sinf(cameraPitch);
      uniformStructure.cameraForward[2] =
          cosf(cameraPitch) * sinf(-cameraYaw - (M_PI / 2.0));

      uniformStructure.cameraRight[0] =
          uniformStructure.cameraForward[1] * uniformStructure.cameraUp[2] -
          uniformStructure.cameraForward[2] * uniformStructure.cameraUp[1];
      uniformStructure.cameraRight[1] =
          uniformStructure.cameraForward[2] * uniformStructure.cameraUp[0] -
          uniformStructure.cameraForward[0] * uniformStructure.cameraUp[2];
      uniformStructure.cameraRight[2] =
          uniformStructure.cameraForward[0] * uniformStructure.cameraUp[1] -
          uniformStructure.cameraForward[1] * uniformStructure.cameraUp[0];

      uniformStructure.frameCount = 0;
    } else {
      uniformStructure.frameCount += 1;
    }

    copyData(uniformDeviceMemoryHandle,
      (void *) &uniformStructure,
      sizeof(UniformStructure));

    result = vkWaitForFences(deviceHandle, 1,
                             &imageAvailableFenceHandleList[currentFrame], true,
                             UINT32_MAX);

    if (result != VK_SUCCESS && result != VK_TIMEOUT) {
      throwExceptionVulkanAPI(result, "vkWaitForFences");
    }

    result = vkResetFences(deviceHandle, 1,
                           &imageAvailableFenceHandleList[currentFrame]);

    if (result != VK_SUCCESS) {
      throwExceptionVulkanAPI(result, "vkResetFences");
    }

    uint32_t currentImageIndex = -1;
    result =
        vkAcquireNextImageKHR(deviceHandle, swapchainHandle, UINT32_MAX,
                              acquireImageSemaphoreHandleList[currentFrame],
                              VK_NULL_HANDLE, &currentImageIndex);

    if (result != VK_SUCCESS) {
      throwExceptionVulkanAPI(result, "vkAcquireNextImageKHR");
    }

    VkPipelineStageFlags pipelineStageFlags =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = NULL,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &acquireImageSemaphoreHandleList[currentFrame],
        .pWaitDstStageMask = &pipelineStageFlags,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBufferHandleList[currentImageIndex],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &writeImageSemaphoreHandleList[currentImageIndex]};

    result = vkQueueSubmit(queueHandle, 1, &submitInfo,
                           imageAvailableFenceHandleList[currentFrame]);

    if (result != VK_SUCCESS) {
      throwExceptionVulkanAPI(result, "vkQueueSubmit");
    }

    VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = NULL,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &writeImageSemaphoreHandleList[currentImageIndex],
        .swapchainCount = 1,
        .pSwapchains = &swapchainHandle,
        .pImageIndices = &currentImageIndex,
        .pResults = NULL};

    result = vkQueuePresentKHR(queueHandle, &presentInfo);

    if (result != VK_SUCCESS) {
      throwExceptionVulkanAPI(result, "vkQueuePresentKHR");
    }

    currentFrame = (currentFrame + 1) % swapchainImageCount;
  }

  // =========================================================================
  // Cleanup

  result = vkDeviceWaitIdle(deviceHandle);

  if (result != VK_SUCCESS) {
    throwExceptionVulkanAPI(result, "vkDeviceWaitIdle");
  }

  for (uint32_t x; x < swapchainImageCount; x++) {
    vkDestroySemaphore(deviceHandle, writeImageSemaphoreHandleList[x], NULL);
    vkDestroySemaphore(deviceHandle, acquireImageSemaphoreHandleList[x], NULL);
    vkDestroyFence(deviceHandle, imageAvailableFenceHandleList[x], NULL);
  }

  delete[] shaderHandleBuffer;
  vkFreeMemory(deviceHandle, shaderBindingTableDeviceMemoryHandle, NULL);
  vkDestroyBuffer(deviceHandle, shaderBindingTableBufferHandle, NULL);

  vkFreeMemory(deviceHandle, materialDeviceMemoryHandle, NULL);
  vkDestroyBuffer(deviceHandle, materialBufferHandle, NULL);
  vkFreeMemory(deviceHandle, materialIndexDeviceMemoryHandle, NULL);
  vkDestroyBuffer(deviceHandle, materialIndexBufferHandle, NULL);
  vkDestroyFence(deviceHandle,
                 rayTraceImageBarrierAccelerationStructureBuildFenceHandle,
                 NULL);

  vkDestroyImageView(deviceHandle, rayTraceImageViewHandle, NULL);
  vkFreeMemory(deviceHandle, rayTraceImageDeviceMemoryHandle, NULL);
  vkDestroyImage(deviceHandle, rayTraceImageHandle, NULL);
  vkFreeMemory(deviceHandle, uniformDeviceMemoryHandle, NULL);
  vkDestroyBuffer(deviceHandle, uniformBufferHandle, NULL);

  for(int i = 0; i < objectCount; i++){
    pvkDestroyAccelerationStructureKHR(deviceHandle,
                                    topLevelAccelerationStructureHandle[i], NULL);

    vkFreeMemory(deviceHandle, topLevelAccelerationStructureDeviceMemoryHandle[i],
                NULL);

    vkDestroyBuffer(deviceHandle, topLevelAccelerationStructureBufferHandle[i],
                    NULL);
  }

  vkDestroyFence(deviceHandle, bottomLevelAccelerationStructureBuildFenceHandle,
                 NULL);

  for(int i = 0; i < objectCount; i++){

    vkFreeMemory(deviceHandle,
                bottomLevelAccelerationStructureDeviceScratchMemoryHandle[i], NULL);

    vkDestroyBuffer(deviceHandle,
                    bottomLevelAccelerationStructureScratchBufferHandle[i], NULL);

    pvkDestroyAccelerationStructureKHR(
      deviceHandle, bottomLevelAccelerationStructureHandle[i], NULL);

    vkFreeMemory(deviceHandle, bottomLevelAccelerationStructureDeviceMemoryHandle[i],
               NULL);

    vkDestroyBuffer(deviceHandle, bottomLevelAccelerationStructureBufferHandle[i],
                  NULL);

    vkFreeMemory(deviceHandle, indexDeviceMemoryHandle[i], NULL);
    vkDestroyBuffer(deviceHandle, indexBufferHandle[i], NULL);
    vkFreeMemory(deviceHandle, vertexDeviceMemoryHandle[i], NULL);
    vkDestroyBuffer(deviceHandle, vertexBufferHandle[i], NULL);
   }

  vkDestroyPipeline(deviceHandle, rayTracingPipelineHandle, NULL);
  vkDestroyShaderModule(deviceHandle, rayMissShadowShaderModuleHandle, NULL);
  vkDestroyShaderModule(deviceHandle, rayMissShaderModuleHandle, NULL);
  vkDestroyShaderModule(deviceHandle, rayGenerateShaderModuleHandle, NULL);
  vkDestroyShaderModule(deviceHandle, rayClosestHitShaderModuleHandle, NULL);
  vkDestroyPipelineLayout(deviceHandle, pipelineLayoutHandle, NULL);
  vkDestroyDescriptorSetLayout(deviceHandle, materialDescriptorSetLayoutHandle,
                               NULL);

  vkDestroyDescriptorSetLayout(deviceHandle, descriptorSetLayoutHandle, NULL);
  vkDestroyDescriptorPool(deviceHandle, descriptorPoolHandle, NULL);

  for (uint32_t x = 0; x < swapchainImageCount; x++) {
    vkDestroyImageView(deviceHandle, swapchainImageViewHandleList[x], NULL);
  }

  vkDestroySwapchainKHR(deviceHandle, swapchainHandle, NULL);
  vkDestroyCommandPool(deviceHandle, commandPoolHandle, NULL);
  vkDestroyDevice(deviceHandle, NULL);
  vkDestroySurfaceKHR(instanceHandle, surfaceHandle, NULL);
  vkDestroyInstance(instanceHandle, NULL);

  return 0;
}
