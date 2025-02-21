#include <iostream>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring> // for memcpy
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// window dimensions
constexpr int window_width = 800;
constexpr int window_height = 600;

// aspect ratio for perspective transform
static float g_aspect_ratio = float(window_width) / float(window_height);

// variables controlling frame recording
bool g_recording = false;
float g_recordStartTime = 0.0f;
int g_recordedFrames = 0;
float g_globalSimTime = 0.0f;

// simple vertex with position and normal
struct vertex {
    float x, y, z;
    float nx, ny, nz;
};

// key callback
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
    if (key == GLFW_KEY_S && action == GLFW_PRESS) {
        if (!g_recording) {
            g_recording = true;
            g_recordStartTime = g_globalSimTime;
            g_recordedFrames = 0;
            std::cout << "started recording simulation for 16 seconds." << std::endl;
        }
    }
}

// viewport and aspect ratio update
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    if (height == 0) return;
    glViewport(0, 0, width, height);
    g_aspect_ratio = float(width) / float(height);
}

// saves current frame to png
void saveFrame(int frameNumber) {
    unsigned char* pixels = new unsigned char[window_width * window_height * 3];
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, window_width, window_height, GL_RGB, GL_UNSIGNED_BYTE, pixels);

    // flip since opengl origin is bottom-left
    unsigned char* flipped = new unsigned char[window_width * window_height * 3];
    for (int y = 0; y < window_height; ++y) {
        memcpy(flipped + (window_width * 3) * y,
            pixels + (window_width * 3) * (window_height - 1 - y),
            window_width * 3);
    }

    char filename[64];
    sprintf_s(filename, "frame_%04d.png", frameNumber);
    stbi_write_png(filename, window_width, window_height, 3, flipped, window_width * 3);

    delete[] pixels;
    delete[] flipped;
}

// simulates water volume with sinusoidal waves
class watervolume {
public:
    watervolume(int gw, int gd, float w, float d, float t)
        : gridwidth(gw), griddepth(gd), width(w), depth(d), thickness(t)
    {
        buildmesh();
    }

    // combines two waves: wave = amp * sin(freq * (dir dot (pos - speed * time)))
    void updatewaves(float time) {
        float amplitude1 = 0.6f;
        float amplitude2 = 0.3f;
        float frequency1 = 0.8f;
        float frequency2 = 0.6f;
        float speed1 = 0.3f;
        float speed2 = 0.2f;

        glm::vec2 dir1 = glm::normalize(glm::vec2(1.0f, 0.2f));
        glm::vec2 dir2 = glm::normalize(glm::vec2(0.2f, 1.0f));

        // top surface y = wave1 + wave2
        for (int z = 0; z < griddepth; ++z) {
            for (int x = 0; x < gridwidth; ++x) {
                int idx = topstart + x + z * gridwidth;
                float px = vertices[idx].x;
                float pz = vertices[idx].z;
                float dot1 = glm::dot(dir1, glm::vec2(px - speed1 * time, pz - speed1 * time));
                float dot2 = glm::dot(dir2, glm::vec2(px - speed2 * time, pz - speed2 * time));
                float wave1 = amplitude1 * sinf(frequency1 * dot1);
                float wave2 = amplitude2 * sinf(frequency2 * dot2);
                vertices[idx].y = wave1 + wave2;
            }
        }

        // bottom surface offset by thickness
        for (int z = 0; z < griddepth; ++z) {
            for (int x = 0; x < gridwidth; ++x) {
                int top_idx = topstart + x + z * gridwidth;
                int bottom_idx = bottomstart + x + z * gridwidth;
                vertices[bottom_idx].y = vertices[top_idx].y - thickness;
            }
        }

        // normals from partial derivatives of height field
        for (int z = 1; z < griddepth - 1; ++z) {
            for (int x = 1; x < gridwidth - 1; ++x) {
                int idx = topstart + x + z * gridwidth;
                float yl = vertices[idx - 1].y;
                float yr = vertices[idx + 1].y;
                float yd = vertices[idx - gridwidth].y;
                float yu = vertices[idx + gridwidth].y;
                float dx = (yr - yl) * 0.5f;
                float dz = (yu - yd) * 0.5f;
                glm::vec3 n = glm::normalize(glm::vec3(-dx, 1.0f, -dz));
                vertices[idx].nx = n.x;
                vertices[idx].ny = n.y;
                vertices[idx].nz = n.z;
            }
        }

        for (int z = 1; z < griddepth - 1; ++z) {
            for (int x = 1; x < gridwidth - 1; ++x) {
                int idx = bottomstart + x + z * gridwidth;
                float yl = vertices[idx - 1].y;
                float yr = vertices[idx + 1].y;
                float yd = vertices[idx - gridwidth].y;
                float yu = vertices[idx + gridwidth].y;
                float dx = (yr - yl) * 0.5f;
                float dz = (yu - yd) * 0.5f;
                glm::vec3 n = glm::normalize(glm::vec3(dx, -1.0f, dz));
                vertices[idx].nx = n.x;
                vertices[idx].ny = n.y;
                vertices[idx].nz = n.z;
            }
        }
    }

    void upload(GLuint vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(vertex), vertices.data());
    }

    std::vector<vertex> vertices;
    std::vector<unsigned int> indices;

private:
    int gridwidth, griddepth;
    int topstart = 0, bottomstart = 0;
    float width, depth, thickness;

    // builds top and bottom surfaces plus side faces
    void buildmesh() {
        topstart = 0;
        bottomstart = gridwidth * griddepth;
        vertices.resize(gridwidth * griddepth * 2);

        // top surface
        for (int z = 0; z < griddepth; ++z) {
            for (int x = 0; x < gridwidth; ++x) {
                int idx = x + z * gridwidth;
                float fx = static_cast<float>(x) / (gridwidth - 1);
                float fz = static_cast<float>(z) / (griddepth - 1);
                float px = fx * width - 0.5f * width;
                float pz = fz * depth - 0.5f * depth;
                vertices[topstart + idx] = { px, 0.0f, pz, 0, 1, 0 };
            }
        }

        // bottom surface
        for (int z = 0; z < griddepth; ++z) {
            for (int x = 0; x < gridwidth; ++x) {
                int idx = x + z * gridwidth;
                vertices[bottomstart + idx] = {
                    vertices[topstart + idx].x,
                    -thickness,
                    vertices[topstart + idx].z,
                    0, -1, 0
                };
            }
        }

        // triangles for top
        for (int z = 0; z < griddepth - 1; ++z) {
            for (int x = 0; x < gridwidth - 1; ++x) {
                int i0 = topstart + (x + z * gridwidth);
                int i1 = topstart + ((x + 1) + z * gridwidth);
                int i2 = topstart + (x + (z + 1) * gridwidth);
                int i3 = topstart + ((x + 1) + (z + 1) * gridwidth);
                indices.push_back(i0);
                indices.push_back(i1);
                indices.push_back(i2);
                indices.push_back(i1);
                indices.push_back(i3);
                indices.push_back(i2);
            }
        }

        // triangles for bottom
        for (int z = 0; z < griddepth - 1; ++z) {
            for (int x = 0; x < gridwidth - 1; ++x) {
                int i0 = bottomstart + (x + z * gridwidth);
                int i1 = bottomstart + ((x + 1) + z * gridwidth);
                int i2 = bottomstart + (x + (z + 1) * gridwidth);
                int i3 = bottomstart + ((x + 1) + (z + 1) * gridwidth);
                indices.push_back(i0);
                indices.push_back(i2);
                indices.push_back(i1);
                indices.push_back(i1);
                indices.push_back(i2);
                indices.push_back(i3);
            }
        }

        auto addsidequad = [&](int topa, int topb, int bottoma, int bottomb) {
            indices.push_back(topa);
            indices.push_back(topb);
            indices.push_back(bottoma);
            indices.push_back(topb);
            indices.push_back(bottomb);
            indices.push_back(bottoma);
            };

        // left side
        for (int z = 0; z < griddepth - 1; ++z) {
            int topa = topstart + (0 + z * gridwidth);
            int topb = topstart + (0 + (z + 1) * gridwidth);
            int bottoma = bottomstart + (0 + z * gridwidth);
            int bottomb = bottomstart + (0 + (z + 1) * gridwidth);
            addsidequad(topa, topb, bottoma, bottomb);
        }

        // right side
        for (int z = 0; z < griddepth - 1; ++z) {
            int topa = topstart + ((gridwidth - 1) + z * gridwidth);
            int topb = topstart + ((gridwidth - 1) + (z + 1) * gridwidth);
            int bottoma = bottomstart + ((gridwidth - 1) + z * gridwidth);
            int bottomb = bottomstart + ((gridwidth - 1) + (z + 1) * gridwidth);
            indices.push_back(topb);
            indices.push_back(topa);
            indices.push_back(bottomb);
            indices.push_back(topa);
            indices.push_back(bottoma);
            indices.push_back(bottomb);
        }

        // front side
        for (int x = 0; x < gridwidth - 1; ++x) {
            int topa = topstart + (x + 0 * gridwidth);
            int topb = topstart + ((x + 1) + 0 * gridwidth);
            int bottoma = bottomstart + (x + 0 * gridwidth);
            int bottomb = bottomstart + ((x + 1) + 0 * gridwidth);
            indices.push_back(topb);
            indices.push_back(topa);
            indices.push_back(bottomb);
            indices.push_back(topa);
            indices.push_back(bottoma);
            indices.push_back(bottomb);
        }

        // back side
        for (int x = 0; x < gridwidth - 1; ++x) {
            int topa = topstart + (x + (griddepth - 1) * gridwidth);
            int topb = topstart + ((x + 1) + (griddepth - 1) * gridwidth);
            int bottoma = bottomstart + (x + (griddepth - 1) * gridwidth);
            int bottomb = bottomstart + ((x + 1) + (griddepth - 1) * gridwidth);
            addsidequad(topa, topb, bottoma, bottomb);
        }
    }
};

static const char* vertex_shader_source = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
out vec3 vWorldPos;
out vec3 vNormal;
void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uProj * uView * worldPos;
}
)";

static const char* fragment_shader_source = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
out vec4 fragColor;
uniform vec3 uCamPos;
uniform vec3 uLightPos;
uniform int uSteps;
uniform vec3 uDarkColor;
uniform vec3 uLightColor;
void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uLightPos - vWorldPos);
    float lambert = max(dot(normal, lightDir), 0.0);
    float toonLevel = floor(lambert * float(uSteps)) / float(uSteps);
    vec3 color = mix(uDarkColor, uLightColor, toonLevel);
    fragColor = vec4(color, 1.0);
}
)";

// shader compilation
GLuint compileshader(GLenum shadertype, const char* shadersource) {
    GLuint shader = glCreateShader(shadertype);
    glShaderSource(shader, 1, &shadersource, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infolog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infolog);
        std::cerr << "error shader compilation failed\n" << infolog << "\n";
    }
    return shader;
}

// link vertex and fragment shaders
GLuint createshaderprogram(const char* vertexsource, const char* fragmentsource) {
    GLuint vertexshader = compileshader(GL_VERTEX_SHADER, vertexsource);
    GLuint fragmentshader = compileshader(GL_FRAGMENT_SHADER, fragmentsource);
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexshader);
    glAttachShader(program, fragmentshader);
    glLinkProgram(program);
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infolog[512];
        glGetProgramInfoLog(program, 512, nullptr, infolog);
        std::cerr << "error program linking failed\n" << infolog << "\n";
    }
    glDeleteShader(vertexshader);
    glDeleteShader(fragmentshader);
    return program;
}

int main() {
    if (!glfwInit()) {
        std::cerr << "failed to initialize glfw\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(window_width, window_height, "fluid sim :)", nullptr, nullptr);
    if (!window) {
        std::cerr << "failed to create glfw window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "failed to initialize glew\n";
        glfwTerminate();
        return -1;
    }
    glEnable(GL_DEPTH_TEST);

    GLuint shaderprogram = createshaderprogram(vertex_shader_source, fragment_shader_source);

    int   gw = 200;
    int   gd = 200;
    float ww = 300.0f;
    float wd = 200.0f;
    float wt = 2.0f;
    watervolume water(gw, gd, ww, wd, wt);

    GLuint vao, vbo, ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
        water.vertices.size() * sizeof(vertex),
        water.vertices.data(),
        GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        water.indices.size() * sizeof(unsigned int),
        water.indices.data(),
        GL_STATIC_DRAW);
    glBindVertexArray(0);

    glm::vec3 camerapos(0, 50, 100);
    glm::vec3 lightpos(80, 80, 80);
    glm::mat4 model = glm::mat4(1.0f);

    float timeaccumulator = 0.0f;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        timeaccumulator += 0.05f;
        g_globalSimTime = timeaccumulator;

        water.updatewaves(timeaccumulator);
        water.upload(vbo);

        glClearColor(0.3f, 0.5f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderprogram);
        glm::mat4 view = glm::lookAt(
            camerapos,
            glm::vec3(0, 0, 0),
            glm::vec3(0, 1, 0)
        );
        glm::mat4 projection = glm::perspective(
            glm::radians(45.0f),
            g_aspect_ratio,
            0.1f,
            500.0f
        );

        glUniformMatrix4fv(glGetUniformLocation(shaderprogram, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(glGetUniformLocation(shaderprogram, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderprogram, "uProj"), 1, GL_FALSE, glm::value_ptr(projection));

        glUniform3fv(glGetUniformLocation(shaderprogram, "uCamPos"), 1, glm::value_ptr(camerapos));
        glUniform3fv(glGetUniformLocation(shaderprogram, "uLightPos"), 1, glm::value_ptr(lightpos));
        glUniform1i(glGetUniformLocation(shaderprogram, "uSteps"), 3);

        glm::vec3 darkcolor(0.0f, 0.0f, 0.5f);
        glm::vec3 lightcolor(0.3f, 0.6f, 1.0f);
        glUniform3fv(glGetUniformLocation(shaderprogram, "uDarkColor"), 1, glm::value_ptr(darkcolor));
        glUniform3fv(glGetUniformLocation(shaderprogram, "uLightColor"), 1, glm::value_ptr(lightcolor));

        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES,
            static_cast<GLsizei>(water.indices.size()),
            GL_UNSIGNED_INT,
            0);

        if (g_recording) {
            saveFrame(g_recordedFrames);
            g_recordedFrames++;
            if (g_globalSimTime - g_recordStartTime >= 16.0f) {
                g_recording = false;
                float fps = g_recordedFrames / 16.0f;
                char cmd[256];
                sprintf_s(cmd, "ffmpeg -y -framerate %.2f -i frame_%%04d.png -c:v libx264 -pix_fmt yuv420p simulation.mp4", fps);
                std::cout << "finished recording. running command: " << cmd << std::endl;
                system(cmd);
                std::cout << "video saved as simulation.mp4" << std::endl;
            }
        }

        glfwSwapBuffers(window);
    }

    glDeleteProgram(shaderprogram);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteVertexArrays(1, &vao);
    glfwTerminate();
    return 0;
}
