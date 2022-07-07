#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

#define M_PI 3.1415926535897932384626433832795

hitAttributeEXT vec2 hitCoordinate;

layout(location = 0) rayPayloadInEXT Payload {
  vec3 hitPosition;
  vec3 hitNormal;
  int objectIndex;
}
payload;

layout(location = 1) rayPayloadEXT bool isShadow;

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;

layout(binding = 2, set = 0) buffer IndexBuffer { uint data[]; }
indexBuffer;
layout(binding = 3, set = 0) buffer VertexBuffer { float data[]; }
vertexBuffer;

void main() {
  ivec3 indices = ivec3(indexBuffer.data[3 * gl_PrimitiveID + 0],
                        indexBuffer.data[3 * gl_PrimitiveID + 1],
                        indexBuffer.data[3 * gl_PrimitiveID + 2]);

  vec3 barycentric = vec3(1.0 - hitCoordinate.x - hitCoordinate.y,
                          hitCoordinate.x, hitCoordinate.y);

  vec3 vertexA = vec3(vertexBuffer.data[6 * indices.x + 0],
                      vertexBuffer.data[6 * indices.x + 1],
                      vertexBuffer.data[6 * indices.x + 2]);
  vec3 normalA = vec3(vertexBuffer.data[6 * indices.x + 3],
                      vertexBuffer.data[6 * indices.x + 4],
                      vertexBuffer.data[6 * indices.x + 5]);
  vec3 vertexB = vec3(vertexBuffer.data[6 * indices.y + 0],
                      vertexBuffer.data[6 * indices.y + 1],
                      vertexBuffer.data[6 * indices.y + 2]);
  vec3 normalB = vec3(vertexBuffer.data[6 * indices.y + 3],
                      vertexBuffer.data[6 * indices.y + 4],
                      vertexBuffer.data[6 * indices.y + 5]);
  vec3 vertexC = vec3(vertexBuffer.data[6 * indices.z + 0],
                      vertexBuffer.data[6 * indices.z + 1],
                      vertexBuffer.data[6 * indices.z + 2]);
  vec3 normalC = vec3(vertexBuffer.data[6 * indices.z + 3],
                      vertexBuffer.data[6 * indices.z + 4],
                      vertexBuffer.data[6 * indices.z + 5]);

  vec3 position = vertexA * barycentric.x + vertexB * barycentric.y +
                  vertexC * barycentric.z;
  vec3 normal = normalA * barycentric.x + normalB * barycentric.y +
                normalC * barycentric.z;

  payload.hitPosition = position;
  payload.hitNormal = normal;
  payload.objectIndex = gl_InstanceCustomIndexEXT;
}
