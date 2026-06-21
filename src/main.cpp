/*
 * glspiral - OpenGL animated 3D spiral demo
 * A visually rich alternative to glxgears
 *
 * Copyright (C) 2024  glspiral contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <GL/glu.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ─── Constants ───────────────────────────────────────────────────────────────

static const int    DEFAULT_WIDTH   = 900;
static const int    DEFAULT_HEIGHT  = 700;
static const char*  WINDOW_TITLE    = "glspiral";

static const int    SPIRAL_ARMS     = 5;
static const int    TURNS_PER_ARM   = 4;
static const int    STEPS_PER_TURN  = 80;
static const float  TUBE_RADIUS     = 0.035f;
static const int    TUBE_SEGMENTS   = 12;

// ─── Shader sources ──────────────────────────────────────────────────────────

static const char* VERT_SRC = R"glsl(
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec3 aNormal;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform vec3 uLightDir;

out vec3 vColor;

void main() {
    vec3 worldNormal = normalize(mat3(uModel) * aNormal);
    float diffuse = max(dot(worldNormal, normalize(uLightDir)), 0.0);
    float ambient = 0.35;
    float light = ambient + (1.0 - ambient) * diffuse;
    vColor = aColor * light;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)glsl";

static const char* FRAG_SRC = R"glsl(
#version 330 core

in  vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
)glsl";

// ─── Math helpers ────────────────────────────────────────────────────────────

struct Vec3 { float x, y, z; };
struct Mat4 { float m[16]; };  // column-major

static Vec3 vec3_add(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
static Vec3 vec3_scale(Vec3 a, float s) { return {a.x*s, a.y*s, a.z*s}; }
static Vec3 vec3_norm(Vec3 a) {
    float l = sqrtf(a.x*a.x + a.y*a.y + a.z*a.z);
    if (l < 1e-6f) return {0,0,1};
    return {a.x/l, a.y/l, a.z/l};
}
static Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return { a.y*b.z - a.z*b.y,
             a.z*b.x - a.x*b.z,
             a.x*b.y - a.y*b.x };
}

static Mat4 mat4_identity() {
    Mat4 m{}; m.m[0]=m.m[5]=m.m[10]=m.m[15]=1.f; return m;
}

static Mat4 mat4_mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c=0;c<4;c++)
        for (int row=0;row<4;row++)
            for (int k=0;k<4;k++)
                r.m[c*4+row] += a.m[k*4+row] * b.m[c*4+k];
    return r;
}

static Mat4 mat4_perspective(float fovY, float aspect, float near, float far) {
    Mat4 m{};
    float f = 1.0f / tanf(fovY * 0.5f);
    m.m[0]  = f / aspect;
    m.m[5]  = f;
    m.m[10] = (far + near) / (near - far);
    m.m[11] = -1.f;
    m.m[14] = (2.f * far * near) / (near - far);
    return m;
}

static Mat4 mat4_rotate_y(float a) {
    Mat4 m = mat4_identity();
    m.m[0]  =  cosf(a); m.m[8]  = sinf(a);
    m.m[2]  = -sinf(a); m.m[10] = cosf(a);
    return m;
}
static Mat4 mat4_rotate_x(float a) {
    Mat4 m = mat4_identity();
    m.m[5]  =  cosf(a); m.m[9]  = -sinf(a);
    m.m[6]  =  sinf(a); m.m[10] =  cosf(a);
    return m;
}
static Mat4 mat4_rotate_z(float a) {
    Mat4 m = mat4_identity();
    m.m[0]  =  cosf(a); m.m[4]  = -sinf(a);
    m.m[1]  =  sinf(a); m.m[5]  =  cosf(a);
    return m;
}
static Mat4 mat4_translate(float tx, float ty, float tz) {
    Mat4 m = mat4_identity();
    m.m[12]=tx; m.m[13]=ty; m.m[14]=tz;
    return m;
}

// ─── HSV → RGB ───────────────────────────────────────────────────────────────

static Vec3 hsv_to_rgb(float h, float s, float v) {
    h = fmodf(h, 1.0f) * 6.0f;
    int   i = (int)h;
    float f = h - i;
    float p = v * (1 - s);
    float q = v * (1 - s * f);
    float t = v * (1 - s * (1 - f));
    switch (i % 6) {
        case 0: return {v,t,p};
        case 1: return {q,v,p};
        case 2: return {p,v,t};
        case 3: return {p,q,v};
        case 4: return {t,p,v};
        default: return {v,p,q};
    }
}

// ─── Geometry builder ────────────────────────────────────────────────────────

struct Vertex {
    Vec3 pos;
    Vec3 color;
    Vec3 normal;
};

/*
 * Build one spiral arm as a tube mesh.
 *
 * arm_index  – [0, SPIRAL_ARMS)  used to rotate each arm around Y
 * hue_offset – colour phase for this arm
 * time       – used to animate pulse
 */
static void build_arm(std::vector<Vertex>& verts,
                      std::vector<unsigned>& indices,
                      int arm_index,
                      float hue_offset,
                      float time)
{
    const int   N_SPINE = TURNS_PER_ARM * STEPS_PER_TURN + 1;
    const float arm_angle_offset = (float)arm_index / SPIRAL_ARMS * 2.f * (float)M_PI;
    const float pulse = 1.0f + 0.12f * sinf(time * 2.f + arm_index * 1.3f);

    // --- generate spine positions & tangents ---
    std::vector<Vec3> spine(N_SPINE);
    std::vector<Vec3> tangent(N_SPINE);

    for (int i = 0; i < N_SPINE; i++) {
        float t = (float)i / (N_SPINE - 1);            // [0..1]
        float theta = t * TURNS_PER_ARM * 2.f * (float)M_PI + arm_angle_offset;
        float r = t * 0.8f * pulse;                    // grows outward
        float y = (t - 0.5f) * 1.6f;                  // height from -0.8 to +0.8
        spine[i] = { cosf(theta) * r, y, sinf(theta) * r };
    }
    // tangents: forward differences
    for (int i = 0; i < N_SPINE; i++) {
        int a = (i > 0) ? i-1 : 0;
        int b = (i < N_SPINE-1) ? i+1 : N_SPINE-1;
        Vec3 d = { spine[b].x-spine[a].x,
                   spine[b].y-spine[a].y,
                   spine[b].z-spine[a].z };
        tangent[i] = vec3_norm(d);
    }

    // --- build tube rings ---
    unsigned base = (unsigned)verts.size();

    for (int i = 0; i < N_SPINE; i++) {
        float t   = (float)i / (N_SPINE - 1);
        float hue = hue_offset + t * 0.6f;
        Vec3  col = hsv_to_rgb(hue, 0.85f, 1.0f);

        // Frenet-like frame
        Vec3 T = tangent[i];
        Vec3 up = { 0,1,0 };
        if (fabsf(T.y) > 0.9f) up = {1,0,0};
        Vec3 B = vec3_norm(vec3_cross(T, up));
        Vec3 N2 = vec3_cross(B, T);

        float tr = TUBE_RADIUS * (0.5f + 0.5f * t); // tapers at center

        for (int s = 0; s < TUBE_SEGMENTS; s++) {
            float a = (float)s / TUBE_SEGMENTS * 2.f * (float)M_PI;
            Vec3 n = vec3_add(vec3_scale(B,  cosf(a)),
                              vec3_scale(N2, sinf(a)));
            Vec3 p = vec3_add(spine[i], vec3_scale(n, tr));
            verts.push_back({ p, col, vec3_norm(n) });
        }
    }

    // --- stitch quads ---
    for (int i = 0; i < N_SPINE - 1; i++) {
        for (int s = 0; s < TUBE_SEGMENTS; s++) {
            int s2 = (s + 1) % TUBE_SEGMENTS;
            unsigned a = base + i * TUBE_SEGMENTS + s;
            unsigned b = base + i * TUBE_SEGMENTS + s2;
            unsigned c = base + (i+1) * TUBE_SEGMENTS + s;
            unsigned d = base + (i+1) * TUBE_SEGMENTS + s2;
            indices.push_back(a); indices.push_back(b); indices.push_back(c);
            indices.push_back(b); indices.push_back(d); indices.push_back(c);
        }
    }
}

// ─── Shader helpers ──────────────────────────────────────────────────────────

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetShaderInfoLog(s, 512, nullptr, log);
        fprintf(stderr, "Shader error: %s\n", log);
    }
    return s;
}

static GLuint build_program() {
    GLuint vs = compile_shader(GL_VERTEX_SHADER,   VERT_SRC);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FRAG_SRC);
    GLuint p  = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

// ─── State ───────────────────────────────────────────────────────────────────

struct AppState {
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLuint program = 0;
    int    index_count = 0;

    GLint  u_mvp      = -1;
    GLint  u_model    = -1;
    GLint  u_light    = -1;

    double last_time  = 0.0;
    double fps_time   = 0.0;
    int    fps_frames = 0;
    float  fps        = 0.f;

    int width  = DEFAULT_WIDTH;
    int height = DEFAULT_HEIGHT;

    // camera rotation (mouse drag)
    bool  dragging    = false;
    double drag_x0    = 0, drag_y0 = 0;
    float  cam_yaw    = 0.f;
    float  cam_pitch  = 0.3f;
    float  drag_yaw0  = 0.f, drag_pitch0 = 0.f;
};

static AppState g_app;

// ─── Geometry upload ─────────────────────────────────────────────────────────

static void upload_geometry(float time) {
    std::vector<Vertex>   verts;
    std::vector<unsigned> indices;

    for (int i = 0; i < SPIRAL_ARMS; i++) {
        float hue = (float)i / SPIRAL_ARMS + time * 0.05f;
        build_arm(verts, indices, i, hue, time);
    }

    g_app.index_count = (int)indices.size();

    glBindVertexArray(g_app.vao);

    glBindBuffer(GL_ARRAY_BUFFER, g_app.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(verts.size() * sizeof(Vertex)),
                 verts.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_app.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(indices.size() * sizeof(unsigned)),
                 indices.data(), GL_DYNAMIC_DRAW);

    // aPos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(0);
    // aColor
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, color));
    glEnableVertexAttribArray(1);
    // aNormal
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
}

// ─── Callbacks ───────────────────────────────────────────────────────────────

static void cb_framebuffer(GLFWwindow*, int w, int h) {
    g_app.width = w; g_app.height = h;
    glViewport(0, 0, w, h);
}

static void cb_mouse_button(GLFWwindow* win, int btn, int action, int) {
    if (btn == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            g_app.dragging = true;
            glfwGetCursorPos(win, &g_app.drag_x0, &g_app.drag_y0);
            g_app.drag_yaw0   = g_app.cam_yaw;
            g_app.drag_pitch0 = g_app.cam_pitch;
        } else {
            g_app.dragging = false;
        }
    }
}

static void cb_cursor(GLFWwindow*, double x, double y) {
    if (!g_app.dragging) return;
    float dx = (float)(x - g_app.drag_x0) * 0.005f;
    float dy = (float)(y - g_app.drag_y0) * 0.005f;
    g_app.cam_yaw   = g_app.drag_yaw0   + dx;
    g_app.cam_pitch = g_app.drag_pitch0 + dy;
    const float MAX_PITCH = 1.4f;
    if (g_app.cam_pitch >  MAX_PITCH) g_app.cam_pitch =  MAX_PITCH;
    if (g_app.cam_pitch < -MAX_PITCH) g_app.cam_pitch = -MAX_PITCH;
}

static void cb_key(GLFWwindow* win, int key, int, int action, int) {
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(win, GLFW_TRUE);
}

// ─── Main ────────────────────────────────────────────────────────────────────

int main() {
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialise GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* win = glfwCreateWindow(DEFAULT_WIDTH, DEFAULT_HEIGHT,
                                       WINDOW_TITLE, nullptr, nullptr);
    if (!win) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate(); return 1;
    }

    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    glfwSetFramebufferSizeCallback(win, cb_framebuffer);
    glfwSetMouseButtonCallback(win,    cb_mouse_button);
    glfwSetCursorPosCallback(win,      cb_cursor);
    glfwSetKeyCallback(win,            cb_key);

    glewExperimental = GL_TRUE;
    GLenum glew_err = glewInit();
    if (glew_err != GLEW_OK && glew_err != GLEW_ERROR_NO_GLX_DISPLAY) {
        fprintf(stderr, "Failed to initialise GLEW: %s\n",
                glewGetErrorString(glew_err));
        return 1;
    }

    printf("glspiral  –  OpenGL %s\n", glGetString(GL_VERSION));
    printf("Controls: left-drag to rotate, ESC to quit\n");

    // ── GL setup ──
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glClearColor(0.04f, 0.04f, 0.08f, 1.f);

    g_app.program = build_program();
    g_app.u_mvp   = glGetUniformLocation(g_app.program, "uMVP");
    g_app.u_model = glGetUniformLocation(g_app.program, "uModel");
    g_app.u_light = glGetUniformLocation(g_app.program, "uLightDir");

    glGenVertexArrays(1, &g_app.vao);
    glGenBuffers(1, &g_app.vbo);
    glGenBuffers(1, &g_app.ebo);

    g_app.last_time = glfwGetTime();
    g_app.fps_time  = g_app.last_time;

    // ── Render loop ──
    while (!glfwWindowShouldClose(win)) {
        double now  = glfwGetTime();
        float  time = (float)now;

        // FPS counter
        g_app.fps_frames++;
        if (now - g_app.fps_time >= 1.0) {
            g_app.fps      = (float)g_app.fps_frames / (float)(now - g_app.fps_time);
            g_app.fps_time = now;
            g_app.fps_frames = 0;
            char title[64];
            snprintf(title, sizeof(title), "glspiral  –  %.0f FPS", g_app.fps);
            glfwSetWindowTitle(win, title);
        }
        g_app.last_time = now;

        // Rebuild geometry every frame (animated)
        upload_geometry(time);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(g_app.program);

        // Model: slow auto-rotation + user drag
        float auto_yaw = time * 0.4f;
        Mat4 rot_y = mat4_rotate_y(auto_yaw + g_app.cam_yaw);
        Mat4 rot_x = mat4_rotate_x(g_app.cam_pitch);
        Mat4 rot_z = mat4_rotate_z(sinf(time * 0.15f) * 0.15f);  // gentle sway
        Mat4 model = mat4_mul(rot_x, mat4_mul(rot_z, rot_y));

        // View: pull back
        Mat4 view = mat4_translate(0.f, 0.f, -2.8f);

        // Projection
        float aspect = (g_app.height > 0) ?
                       (float)g_app.width / (float)g_app.height : 1.f;
        Mat4 proj = mat4_perspective(0.75f, aspect, 0.1f, 100.f);

        Mat4 mvp = mat4_mul(proj, mat4_mul(view, model));

        glUniformMatrix4fv(g_app.u_mvp,   1, GL_FALSE, mvp.m);
        glUniformMatrix4fv(g_app.u_model, 1, GL_FALSE, model.m);

        // Animated light direction
        float lx = cosf(time * 0.7f);
        float ly = 0.6f;
        float lz = sinf(time * 0.7f);
        glUniform3f(g_app.u_light, lx, ly, lz);

        glBindVertexArray(g_app.vao);
        glDrawElements(GL_TRIANGLES, g_app.index_count, GL_UNSIGNED_INT, nullptr);

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    // Cleanup
    glDeleteVertexArrays(1, &g_app.vao);
    glDeleteBuffers(1, &g_app.vbo);
    glDeleteBuffers(1, &g_app.ebo);
    glDeleteProgram(g_app.program);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
