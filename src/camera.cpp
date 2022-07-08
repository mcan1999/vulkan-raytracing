#include <math.h>
#include <glm/gtx/euler_angles.hpp>

#include "camera.h"

#define PITCH_LIMIT (1.57)

Camera::Camera(glm::vec3 initialPosition)
    : position(initialPosition),
      pitch(0.0),
      yaw(-M_PI_2)
{
    updateCameraVectors();
}

void Camera::updateCameraVectors()
{
    float cos_pitch = cos(pitch);
    front.x = cos(yaw) * cos_pitch;
    front.y = sin(pitch);
    front.z = sin(yaw) * cos_pitch;

    right = glm::normalize(glm::vec3(-front.z, 0, front.x));
    up = glm::cross(right, front);
}

// glm::vec3 Camera::getFrontVector()
// {
//     float cos_pitch = cos(pitch);
//     return glm::vec3(
//         sin(yaw) * cos_pitch,
//         sin(pitch),
//         -cos(yaw) * cos_pitch);
// }

// glm::vec3 Camera::getUpVector()
// {
//     float cos_pitch = -sin(pitch);
//     return glm::vec3(
//         -sin(yaw) * cos_pitch,
//         cos(pitch),
//         cos(yaw) * cos_pitch);
// }

// glm::vec3 Camera::getRightVector()
// {
//     float cos_pitch = cos(pitch);
//     return glm::vec3(
//         cos(yaw) * cos_pitch,
//         sin(pitch),
//         sin(yaw) * cos_pitch);
// }

glm::mat4 Camera::getViewingMatrixWithoutTranslation()
{
    glm::mat4 mat = glm::lookAt(glm::vec3(0.0), front, up);
    return mat;
}

glm::mat4 Camera::getViewingMatrix()
{
    glm::mat4 mat = glm::lookAt(position, position + front, up);
    return mat;
}

void Camera::move(CameraMovementDirection dir, float distance)
{
    switch (dir)
    {
    case RIGHT:
        position += distance * right;
        break;
    case LEFT:
        position -= distance * right;
        break;
    case UP:
        position += distance * up;
        break;
    case DOWN:
        position -= distance * up;
        break;
    case FORWARD:
        position += distance * front;
        break;
    case BACKWARD:
        position -= distance * front;
        break;
    }
}

void Camera::processMouseMovement(float xoffset, float yoffset)
{
    yaw += xoffset;
    pitch += yoffset;

    if (pitch > PITCH_LIMIT)
    {
        pitch = PITCH_LIMIT;
    }
    else if (pitch < -PITCH_LIMIT)
    {
        pitch = -PITCH_LIMIT;
    }

    updateCameraVectors();
}

void Camera::look(CameraMovementDirection dir)
{
    switch (dir)
    {
    case RIGHT:
        front = glm::vec3(1, 0, 0);
        up = glm::vec3(0, 1, 0);
        right = glm::vec3(0, 0, 1);
        break;
    case LEFT:
        front = glm::vec3(-1, 0, 0);
        up = glm::vec3(0, 1, 0);
        right = glm::vec3(0, 0, -1);
        break;
    case UP:
        front = glm::vec3(0, 1, 0);
        up = glm::vec3(0, 0, 1);
        right = glm::vec3(1, 0, 0);
        break;
    case DOWN:
        front = glm::vec3(0, -1, 0);
        up = glm::vec3(0, 0, -1);
        right = glm::vec3(1, 0, 0);
        break;
    case FORWARD:
        front = glm::vec3(0, 0, -1);
        up = glm::vec3(0, 1, 0);
        right = glm::vec3(1, 0, 0);
        break;
    case BACKWARD:
        front = glm::vec3(0, 0, 1);
        up = glm::vec3(0, 1, 0);
        right = glm::vec3(-1, 0, 0);
        break;
    }
}