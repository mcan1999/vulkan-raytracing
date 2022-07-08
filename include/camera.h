#ifndef __CAMERA_H__
#define __CAMERA_H__

#include <glm/glm.hpp>

enum CameraMovementDirection
{
    RIGHT = 0,
    LEFT,
    UP,
    DOWN,
    FORWARD,
    BACKWARD,
};

class Camera
{
private:
    glm::vec3 position, front, up, right;
    float pitch, yaw;

    void updateCameraVectors();

public:
    Camera(glm::vec3 initialPosition = glm::vec3(0.0, 0.0, 20.0));
    glm::vec3 getFrontVector() { return front; }
    glm::vec3 getUpVector() { return up; }
    glm::vec3 getRightVector() { return right; }
    glm::mat4 getViewingMatrix();
    glm::mat4 getViewingMatrixWithoutTranslation();
    glm::vec3 getPosition() { return position; }
    void move(CameraMovementDirection dir, float distance);
    void processMouseMovement(float xoffset, float yoffset);
    void look(CameraMovementDirection dir);
};

#endif