#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT Payload {
  vec3 hitPosition;
  vec3 hitNormal;
  int objectIndex;
}
payload;

void main() { payload.objectIndex = -1; }