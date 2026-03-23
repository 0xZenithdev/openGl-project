#include <GL/glut.h>
#include <vector>
#include <deque>
#include <string>
#include <stdlib.h>
#include <time.h>
#include <algorithm>

const float V_X = 400.0f; 
const float V_Y = 300.0f; // Vanishing point at screen center

struct TrailNode { float x, y, z; };

struct GameObject {
    float worldX, worldY, z; 
    float w, h;
    float speed;
    int dir; 
    std::string label;
    bool isCyan;
    std::deque<TrailNode> trail; // For car light trails
};

std::vector<GameObject> buildings;
std::vector<GameObject> cars;
std::vector<GameObject> parallaxStars;

void drawText(float x, float y, std::string text, void* font) {
    glRasterPos2f(x, y);
    for (char c : text) glutBitmapCharacter(font, c);
}

void initScene() {
    srand(time(NULL));
    buildings.clear();
    std::string ads[] = {"MOURN", "XMR", "NEON", "VOID", "DATA"};

    // 1. BUILDINGS: Canyon walls with randomized height and depth
    for (float z = 0.05f; z <= 2.5f; z += 0.15f) {
        float hL = 400 + (rand() % 600);
        float hR = 400 + (rand() % 600);
        std::string sL = (rand() % 4 == 0) ? ads[rand() % 5] : "";
        std::string sR = (rand() % 4 == 0) ? ads[rand() % 5] : "";

        buildings.push_back({ -700.0f, -350.0f, z, 300.0f, hL, 0, 1, sL, (rand() % 2 == 0) });
        buildings.push_back({ 400.0f, -350.0f, z, 300.0f, hR, 0, 1, sR, (rand() % 2 != 0) });
    }

    // 2. CARS: Staggered start positions
    cars.clear();
    for (int i = 0; i < 12; i++) {
        int d = (rand() % 2 == 0) ? 1 : -1;
        float zStart = (float)(rand() % 200) / 100.0f;
        float speed = 0.003f + (float)(rand() % 20) / 10000.0f;
        float lane = (d == 1) ? -180.0f : 180.0f;
        cars.push_back({ lane, (float)(rand() % 200 - 50), zStart, 40.0f, 12.0f, speed, d, "", (rand() % 2 == 0) });
    }
}

void drawPerspectiveBuilding(GameObject b) {
    float s = b.z;
    // Calculate 2D coordinates based on depth
    float x1 = V_X + (b.worldX * s);
    float y1 = V_Y + (b.worldY * s);
    float x2 = V_X + ((b.worldX + b.w) * s);
    float y2 = V_Y + ((b.worldY + b.h) * s);

    // 1. Facade (The front face)
    glColor3f(0.01, 0.01, 0.03);
    glBegin(GL_QUADS);
        glVertex2f(x1, y1); glVertex2f(x2, y1);
        glVertex2f(x2, y2); glVertex2f(x1, y2);
    glEnd();

    // 2. Realistic Window Panes (Z-scaled)
    float g = b.isCyan ? 0.8 : 0.2;
    float bl = 1.0;
    for (float wy = 0.1f; wy < 0.9f; wy += 0.1f) {
        for (float wx = 0.2f; wx < 0.8f; wx += 0.2f) {
            if (((int)(wx * 10 + wy * 20 + b.z * 5) % 9) == 0) continue; 
            glColor3f(0, g * 0.4, bl * 0.4);
            float wx1 = x1 + (x2 - x1) * wx;
            float wy1 = y1 + (y2 - y1) * wy;
            glRectf(wx1, wy1, wx1 + (x2 - x1) * 0.12, wy1 + (y2 - y1) * 0.06);
        }
    }

    // 3. Neon Ad Sign
    if (!b.label.empty()) {
        glColor3f(1.0, 0.0, 0.5); // Magenta Sign
        glRectf(x1 + (x2 - x1) * 0.3, y1 + (y2 - y1) * 0.6, x1 + (x2 - x1) * 0.7, y1 + (y2 - y1) * 0.75);
        glColor3f(1, 1, 1);
        drawText(x1 + (x2 - x1) * 0.35, y1 + (y2 - y1) * 0.65, b.label, GLUT_BITMAP_HELVETICA_12);
    }

    // 4. Structural Outline
    glLineWidth(1.2 * s);
    if (b.isCyan) glColor3f(0, 1, 1); else glColor3f(0.2, 0.4, 1);
    glBegin(GL_LINE_LOOP);
        glVertex2f(x1, y1); glVertex2f(x2, y1);
        glVertex2f(x2, y2); glVertex2f(x1, y2);
    glEnd();
}

void drawEnvironment() {
    // Sky Gradient
    glBegin(GL_QUADS);
        glColor3f(0.04, 0.0, 0.08); glVertex2i(0, 600); glVertex2i(800, 600);
        glColor3f(0, 0, 0); glVertex2i(800, V_Y); glVertex2i(0, V_Y);
    glEnd();

    // Fog (Covering the bottom of the buildings)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
        glColor4f(0, 0.3, 0.4, 0); glVertex2f(0, V_Y + 50); glVertex2f(800, V_Y + 50);
        glColor4f(0, 0.8, 1.0, 0.7); glVertex2f(800, 0); glVertex2f(0, 0);
    glEnd();
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    drawEnvironment();

    std::sort(buildings.begin(), buildings.end(), [](const GameObject& a, const GameObject& b) { return a.z < b.z; });
    for (auto& b : buildings) drawPerspectiveBuilding(b);

    // Draw Cars and Trails
    for (auto& c : cars) {
        // Draw Trails first
        glBegin(GL_LINE_STRIP);
        for (size_t i = 0; i < c.trail.size(); i++) {
            float ts = c.trail[i].z;
            float tx = V_X + (c.trail[i].x * ts);
            float ty = V_Y + (c.trail[i].y * ts);
            float alpha = (float)i / c.trail.size();
            if (c.isCyan) glColor4f(0, 1, 1, alpha * 0.5); else glColor4f(1, 0, 1, alpha * 0.5);
            glVertex2f(tx, ty);
        }
        glEnd();

        // Draw Car Body
        float s = c.z;
        float px = V_X + (c.worldX * s);
        float py = V_Y + (c.worldY * s);
        float pw = c.w * s;
        float ph = c.h * s;
        if (c.isCyan) glColor3f(0, 1, 1); else glColor3f(1, 0, 1);
        glRectf(px - pw / 2, py, px + pw / 2, py + ph);
    }

    glutSwapBuffers();
}

void timer(int) {
    for (auto& c : cars) {
        // Record trail node
        c.trail.push_back({c.worldX, c.worldY, c.z});
        if (c.trail.size() > 15) c.trail.pop_front();

        // Constant movement
        if (c.dir == 1) {
            c.z += c.speed;
            if (c.z > 2.5f) { c.z = 0.05f; c.worldY = rand() % 200 - 50; c.trail.clear(); }
        } else {
            c.z -= c.speed;
            if (c.z < 0.05f) { c.z = 2.5f; c.worldY = rand() % 200 - 50; c.trail.clear(); }
        }
    }
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(800, 600);
    glutCreateWindow("Cyberpunk Final Perspective");
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, 800, 0, 600);
    initScene();
    glutDisplayFunc(display);
    glutTimerFunc(0, timer, 0);
    glutMainLoop();
    return 0;
}