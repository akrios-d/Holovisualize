// Holovisualize preview — KCP consumer
//
// Connects to the server via KCP (UDP) and renders the received mesh live
// using OpenGL 3.3 + GLFW. Like the original LiveScan3D server preview.
//
// Usage: preview [server_ip] [server_kcp_port] [session]
//   Defaults: 127.0.0.1  8081  demo
//
// Controls: left-drag orbit, scroll zoom, R reset, Q/Esc quit.

#include "MeshFrame.h"   // Mesh, decodeMesh (from server/include)

#include <ikcp.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   typedef int socklen_t;
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  define closesocket close
#endif

// GLEW must be included before any OpenGL or GLFW header.
// It provides all OpenGL 1.2+ extension entry points on Windows/Linux.
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#ifdef _WIN32
#  define _USE_MATH_DEFINES   // expose M_PI in <cmath> on MSVC
#endif
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ─── Shared state ─────────────────────────────────────────────────────────────

static std::mutex            g_meshMu;
static Mesh                  g_mesh;
static std::atomic<bool>     g_meshDirty{false};
static std::atomic<bool>     g_connected{false};
static std::atomic<uint64_t> g_frameCount{0};
static std::atomic<bool>     g_running{true};

// ─── KCP helpers ──────────────────────────────────────────────────────────────

static int g_udpFd = -1;
static sockaddr_in g_serverAddr{};

static int kcpOutput(const char* buf, int len, ikcpcb* /*kcp*/, void* /*user*/) {
    sendto(g_udpFd, buf, len, 0,
           reinterpret_cast<const sockaddr*>(&g_serverAddr),
           sizeof(g_serverAddr));
    return 0;
}

static uint32_t nowMs() {
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(
            steady_clock::now().time_since_epoch()).count());
}

// ─── KCP consumer thread ──────────────────────────────────────────────────────

static void kcpThread(const std::string& serverIp, int serverPort,
                      const std::string& session) {
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    g_udpFd = static_cast<int>(socket(AF_INET, SOCK_DGRAM, 0));
    if (g_udpFd < 0) { fprintf(stderr, "[preview] UDP socket failed\n"); return; }

    // 10 ms receive timeout so the loop can be interrupted.
#ifdef _WIN32
    DWORD tv = 10;
    setsockopt(g_udpFd, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    struct timeval tv{0, 10'000};
    setsockopt(g_udpFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    memset(&g_serverAddr, 0, sizeof(g_serverAddr));
    g_serverAddr.sin_family = AF_INET;
    g_serverAddr.sin_port   = htons(static_cast<uint16_t>(serverPort));
    inet_pton(AF_INET, serverIp.c_str(), &g_serverAddr.sin_addr);

    // ── Registration handshake (plain UDP, not KCP) ──────────────────────────
    // Packet: "HVKC" + session_key (null-terminated)
    std::vector<char> regPkt(4 + session.size() + 1);
    memcpy(regPkt.data(), "HVKC", 4);
    memcpy(regPkt.data() + 4, session.c_str(), session.size() + 1);

    uint32_t conv = 0;
    // Retry registration until we get a conv assigned.
    for (int attempt = 0; attempt < 30 && g_running; ++attempt) {
        sendto(g_udpFd, regPkt.data(), static_cast<int>(regPkt.size()), 0,
               reinterpret_cast<const sockaddr*>(&g_serverAddr),
               sizeof(g_serverAddr));

        char resp[64];
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        int n = recvfrom(g_udpFd, resp, sizeof(resp), 0,
                         reinterpret_cast<sockaddr*>(&from), &fromLen);

        if (n >= 12
            && memcmp(resp, "HVKC", 4) == 0
            && memcmp(resp + 8, "OK\0\0", 4) == 0)
        {
            memcpy(&conv, resp + 4, 4);
            g_connected = true;
            printf("[preview] registered with server, conv=%u\n", conv);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (!g_connected) {
        fprintf(stderr, "[preview] could not register with server\n");
        closesocket(g_udpFd);
        return;
    }

    // ── Create KCP context ───────────────────────────────────────────────────
    ikcpcb* kcp = ikcp_create(conv, nullptr);
    ikcp_setoutput(kcp, kcpOutput);
    ikcp_nodelay(kcp, 1, 10, 2, 1);
    ikcp_setmtu(kcp, 1400);
    ikcp_wndsize(kcp, 4096, 4096);

    // ── Receive loop ─────────────────────────────────────────────────────────
    char recvBuf[65536];
    sockaddr_in from{};
    socklen_t fromLen = sizeof(from);

    while (g_running) {
        int n = recvfrom(g_udpFd, recvBuf, sizeof(recvBuf), 0,
                         reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n > 0) {
            ikcp_input(kcp, recvBuf, n);
        }

        ikcp_update(kcp, nowMs());

        // Drain all complete messages.
        while (true) {
            int peekSize = ikcp_peeksize(kcp);
            if (peekSize <= 0) break;

            std::vector<char> frameBuf(peekSize);
            int received = ikcp_recv(kcp, frameBuf.data(), peekSize);
            if (received <= 0) break;

            Mesh m;
            if (decodeMesh(reinterpret_cast<const uint8_t*>(frameBuf.data()),
                           static_cast<size_t>(received), m)) {
                std::lock_guard<std::mutex> lock(g_meshMu);
                g_mesh = std::move(m);
                g_meshDirty = true;
                ++g_frameCount;
            }
        }
    }

    ikcp_release(kcp);
    closesocket(g_udpFd);
#ifdef _WIN32
    WSACleanup();
#endif
}

// ─── OpenGL ───────────────────────────────────────────────────────────────────

static const char* kVertSrc = R"glsl(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNorm;
uniform mat4 uMVP;
uniform mat3 uNormalMat;
out vec3 vNorm;
void main(){
    vNorm = normalize(uNormalMat * aNorm);
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)glsl";

static const char* kFragSrc = R"glsl(
#version 330 core
in  vec3 vNorm;
out vec4 fragColor;
void main(){
    vec3 light = normalize(vec3(0.5, 1.0, 0.8));
    float diff = abs(dot(normalize(vNorm), light));
    vec3 col = vec3(0.15, 0.55, 0.90) * (0.3 + 0.7 * diff);
    fragColor = vec4(col, 1.0);
}
)glsl";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(sh,512,nullptr,log); fprintf(stderr,"%s\n",log); }
    return sh;
}

static GLuint buildProgram() {
    GLuint vs = compileShader(GL_VERTEX_SHADER,   kVertSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragSrc);
    GLuint p  = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

// Column-major mat4
struct Mat4 { float m[16]={}; };

static Mat4 perspective(float fovY, float aspect, float zn, float zf) {
    Mat4 r{};
    float f = 1.f / tanf(fovY * 0.5f);
    r.m[0]=f/aspect; r.m[5]=f;
    r.m[10]=(zf+zn)/(zn-zf); r.m[11]=-1.f;
    r.m[14]=2.f*zf*zn/(zn-zf);
    return r;
}
static Mat4 lookAt(float ex,float ey,float ez,float cx,float cy,float cz){
    float fx=cx-ex,fy=cy-ey,fz=cz-ez,fl=sqrtf(fx*fx+fy*fy+fz*fz);
    fx/=fl;fy/=fl;fz/=fl;
    float rx=fy*0.f-fz*1.f,ry=fz*0.f-fx*0.f,rz=fx*1.f-fy*0.f;
    float rl=sqrtf(rx*rx+ry*ry+rz*rz);rx/=rl;ry/=rl;rz/=rl;
    float ux=ry*fz-rz*fy,uy=rz*fx-rx*fz,uz=rx*fy-ry*fx;
    Mat4 v{};
    v.m[0]=rx;v.m[4]=ry;v.m[8]=rz;
    v.m[1]=ux;v.m[5]=uy;v.m[9]=uz;
    v.m[2]=-fx;v.m[6]=-fy;v.m[10]=-fz;
    v.m[12]=-(rx*ex+ry*ey+rz*ez);
    v.m[13]=-(ux*ex+uy*ey+uz*ez);
    v.m[14]= (fx*ex+fy*ey+fz*ez);
    v.m[15]=1.f;
    return v;
}
static Mat4 mul(const Mat4& a,const Mat4& b){
    Mat4 r{};
    for(int col=0;col<4;col++)for(int row=0;row<4;row++)for(int k=0;k<4;k++)
        r.m[col*4+row]+=a.m[k*4+row]*b.m[col*4+k];
    return r;
}

// ─── Camera ───────────────────────────────────────────────────────────────────

static float g_yaw=0,g_pitch=20,g_dist=2.5f;
static float g_cx=0,g_cy=0.8f,g_cz=0;
static bool g_drag=false;
static double g_lastX=0,g_lastY=0;

static void onMouseBtn(GLFWwindow*,int btn,int act,int){ if(btn==GLFW_MOUSE_BUTTON_LEFT) g_drag=(act==GLFW_PRESS); }
static void onMouseMove(GLFWwindow*,double x,double y){
    if(g_drag){ g_yaw+=(float)(x-g_lastX)*0.4f; g_pitch+=(float)(y-g_lastY)*0.4f;
    if(g_pitch>89)g_pitch=89;if(g_pitch<-89)g_pitch=-89; }
    g_lastX=x;g_lastY=y;
}
static void onScroll(GLFWwindow*,double,double dy){ g_dist-=(float)dy*0.15f; if(g_dist<0.3f)g_dist=0.3f; }
static void onKey(GLFWwindow* w,int key,int,int act,int){
    if(act!=GLFW_PRESS)return;
    if(key==GLFW_KEY_Q||key==GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w,GLFW_TRUE);
    if(key==GLFW_KEY_R){g_yaw=0;g_pitch=20;g_dist=2.5f;}
}

// ─── Entry point ──────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::string serverIp   = "127.0.0.1";
    int         serverPort = 8081;
    std::string session    = "demo";

    if (argc >= 2) serverIp   = argv[1];
    if (argc >= 3) serverPort = std::stoi(argv[2]);
    if (argc >= 4) session    = argv[3];

    // Validate session key before use (security: apply same rules as server).
    if (session.empty() || session.size() > 63) {
        fprintf(stderr, "Invalid session key\n"); return 1;
    }

    // Start KCP consumer thread.
    std::thread kcp(kcpThread, serverIp, serverPort, session);

    // ── GLFW + OpenGL ─────────────────────────────────────────────────────────
    if (!glfwInit()) { fprintf(stderr,"GLFW init failed\n"); g_running=false; kcp.join(); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Holovisualize Preview", nullptr, nullptr);
    if (!window) { fprintf(stderr,"Window failed\n"); g_running=false; kcp.join(); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "GLEW init failed\n");
        g_running = false; kcp.join(); glfwTerminate(); return 1;
    }
    glfwSwapInterval(1);
    glfwSetMouseButtonCallback(window, onMouseBtn);
    glfwSetCursorPosCallback(window,   onMouseMove);
    glfwSetScrollCallback(window,      onScroll);
    glfwSetKeyCallback(window,         onKey);

    GLuint prog = buildProgram();
    GLint uMVP       = glGetUniformLocation(prog, "uMVP");
    GLint uNormalMat = glGetUniformLocation(prog, "uNormalMat");

    GLuint vao, vbo, ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    // Vertex layout: xyz (f32×3) + normals (f32×3) = 24 bytes
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,24,(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,24,(void*)12);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);

    GLsizei g_indexCount = 0;
    auto lastStats = std::chrono::steady_clock::now();
    uint64_t lastFrame = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (g_meshDirty.exchange(false)) {
            Mesh local;
            { std::lock_guard<std::mutex> lock(g_meshMu); local = g_mesh; }
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         local.vertices.size()*sizeof(Vertex),
                         local.vertices.data(), GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         local.indices.size()*sizeof(uint32_t),
                         local.indices.data(), GL_DYNAMIC_DRAW);
            g_indexCount = (GLsizei)local.indices.size();
        }

        int w,h; glfwGetFramebufferSize(window,&w,&h);
        glViewport(0,0,w,h);
        glClearColor(0.08f,0.08f,0.10f,1.f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

        float yr=g_yaw*(float)M_PI/180.f, pr=g_pitch*(float)M_PI/180.f;
        float ex=g_cx+g_dist*cosf(pr)*sinf(yr);
        float ey=g_cy+g_dist*sinf(pr);
        float ez=g_cz+g_dist*cosf(pr)*cosf(yr);
        Mat4 proj=perspective(45.f*(float)M_PI/180.f,(float)w/std::max(h,1),0.05f,50.f);
        Mat4 view=lookAt(ex,ey,ez,g_cx,g_cy,g_cz);
        Mat4 mvp=mul(proj,view);
        float nm[9]={view.m[0],view.m[1],view.m[2],
                     view.m[4],view.m[5],view.m[6],
                     view.m[8],view.m[9],view.m[10]};

        glUseProgram(prog);
        glUniformMatrix4fv(uMVP,1,GL_FALSE,mvp.m);
        glUniformMatrix3fv(uNormalMat,1,GL_FALSE,nm);
        glBindVertexArray(vao);
        if (g_indexCount > 0)
            glDrawElements(GL_TRIANGLES,g_indexCount,GL_UNSIGNED_INT,nullptr);
        glBindVertexArray(0);

        // Title bar stats
        auto now = std::chrono::steady_clock::now();
        if (now - lastStats > std::chrono::seconds(1)) {
            uint64_t fc=g_frameCount.load(), fps=fc-lastFrame;
            lastFrame=fc; lastStats=now;
            char title[160];
            snprintf(title,sizeof(title),
                     "Holovisualize Preview | %s | %llu fps | %d tris",
                     g_connected?"connected":"connecting...",
                     (unsigned long long)fps, g_indexCount/3);
            glfwSetWindowTitle(window,title);
        }

        glfwSwapBuffers(window);
    }

    g_running = false;
    kcp.join();
    glDeleteVertexArrays(1,&vao);
    glDeleteBuffers(1,&vbo);
    glDeleteBuffers(1,&ebo);
    glDeleteProgram(prog);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
