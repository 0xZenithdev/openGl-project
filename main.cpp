/*
    CYBERPUNK METROPOLIS — ISOMETRIC EDITION
    ========================================
    Refactored for Isometric Projection to fix perspective artifacts.
    Features:
      • Linear Isometric Projection (No vanishing point clumping)
      • Depth-sorted rendering (Painter's Algorithm)
      • Solid 3D Building Geometry (Front, Side, Top faces)
      • Isometric Traffic System with Neon Trails
      • "Mourn" Platform Easter Egg Sign
*/

#ifdef __APPLE__
  #include <GLUT/glut.h>
#else
  #include <GL/glut.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const int   SW = 960;
static const int   SH = 720;

// ---------------------------------------------------------------------------
// Math helpers
// ---------------------------------------------------------------------------
struct V2 { float x, y; };

inline float randF(float a, float b) {
    return a + (b - a) * (rand() / float(RAND_MAX));
}

// ISOMETRIC PROJECTION
// Transfroms 3D World (x, y, z) to 2D Screen (sx, sy)
inline V2 project(float x, float y, float z) {
    // Standard Isometric: X and Z axes are 120 degrees apart
    // sx = (x - z) * cos(30 deg)
    // sy = (x + z) * sin(30 deg) + y
    float sx = (x - z) * 0.866f + (SW * 0.5f);
    float sy = (x + z) * 0.500f + y + (SH * 0.1f);
    return { sx, sy };
}

inline void col(float r, float g, float b, float a = 1.f) {
    glColor4f(r, g, b, a);
}

// ---------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------
struct Star { float sx, sy, brightness; };
static std::vector<Star> stars;

struct Building {
    float x, y, z;          
    float w, h, d;          
    bool  leftBank;          
    bool  cyanTrim;          
    std::string signText;
    float signFlicker;       
    float signPhase;         
};

struct TrailPt { float x, y, z; };
struct Car {
    float x, y, z;         
    float speed;      
    bool  forward;  
    bool  cyan;
    float len, wid, tall;
    std::deque<TrailPt> trail;
};

static std::vector<Building> buildings;
static std::vector<Car> cars;

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------
void makeStars() {
    stars.clear();
    for (int i = 0; i < 150; ++i) {
        stars.push_back({ randF(0, SW), randF(SH*0.4f, SH), randF(0.4f, 1.0f) });
    }
}

void spawnBuilding(bool left) {
    Building b;
    b.leftBank = left;
    b.cyanTrim = (rand() % 2 == 0);
    b.w = randF(100, 200);
    b.h = randF(300, 700);
    b.d = randF(100, 200);
    
    // Spread them out along the isometric Z axis
    b.z = randF(-1000, 1000);
    b.y = 0; 
    
    // Position them on either side of the road
    if (left) b.x = randF(-800, -300);
    else      b.x = randF(150, 600);

    static const char* ads[] = { "MOURN", "NEON", "VOID", "DATA", "SYNTH", "NULL" };
    b.signText = (rand() % 3 == 0) ? ads[rand() % 6] : "";
    b.signPhase = randF(0, 6.28f);
    b.signFlicker = 1.0f;
    buildings.push_back(b);
}

void spawnCar(int lane) {
    Car c;
    float laneWidth = 60.0f;
    c.x = -120.0f + (lane * laneWidth);
    c.y = 0;
    c.forward = (lane > 1);
    c.z = c.forward ? -1200 : 1200;
    c.speed = randF(10, 20);
    c.cyan = (rand() % 2 == 0);
    c.len = 60; c.wid = 30; c.tall = 20;
    cars.push_back(c);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void quad4(V2 a, V2 b, V2 c, V2 d) {
    glBegin(GL_QUADS);
        glVertex2f(a.x, a.y); glVertex2f(b.x, b.y);
        glVertex2f(c.x, c.y); glVertex2f(d.x, d.y);
    glEnd();
}

void drawBuilding(const Building& b) {
    // 8 points for a solid box
    V2 fbl = project(b.x,       b.y,       b.z);
    V2 fbr = project(b.x + b.w, b.y,       b.z);
    V2 bbl = project(b.x,       b.y,       b.z + b.d);
    V2 bbr = project(b.x + b.w, b.y,       b.z + b.d);
    
    V2 ftl = project(b.x,       b.y + b.h, b.z);
    V2 ftr = project(b.x + b.w, b.y + b.h, b.z);
    V2 btl = project(b.x,       b.y + b.h, b.z + b.d);
    V2 btr = project(b.x + b.w, b.y + b.h, b.z + b.d);

    // DRAW FACES (Depth sorted: Back-Side -> Top -> Front)
    
    // Side facing road
    col(0.05f, 0.05f, 0.1f);
    if (b.leftBank) quad4(fbr, bbr, btr, ftr); // Right side
    else            quad4(fbl, bbl, btl, ftl); // Left side

    // Top
    col(0.08f, 0.08f, 0.15f);
    quad4(ftl, ftr, btr, btl);

    // Front
    col(0.06f, 0.06f, 0.12f);
    quad4(fbl, fbr, ftr, ftl);

    // Neon Accents
    if (b.cyanTrim) col(0, 1, 1, 0.8f); else col(1, 0, 1, 0.8f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(fbl.x, fbl.y); glVertex2f(fbr.x, fbr.y);
        glVertex2f(ftr.x, ftr.y); glVertex2f(ftl.x, ftl.y);
    glEnd();

    // Sign
    if (!b.signText.empty()) {
        float f = b.signFlicker;
        col(f, f, f);
        glRasterPos2f(ftl.x + 10, ftl.y - 30);
        for (char c : b.signText) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, c);
    }
}

void drawCar(const Car& c) {
    V2 p = project(c.x, c.y, c.z);
    V2 f = project(c.x + c.wid, c.y + c.tall, c.z + c.len);

    // Draw Trail
    if (c.trail.size() > 1) {
        glBegin(GL_LINE_STRIP);
        for (size_t i = 0; i < c.trail.size(); ++i) {
            V2 tp = project(c.trail[i].x, c.trail[i].y, c.trail[i].z);
            if (c.cyan) col(0, 1, 1, (float)i/c.trail.size());
            else col(1, 0, 1, (float)i/c.trail.size());
            glVertex2f(tp.x, tp.y);
        }
        glEnd();
    }

    // Car Body (Simplified Box)
    col(0.2f, 0.2f, 0.2f);
    V2 bfl = project(c.x, c.y, c.z);
    V2 ftr = project(c.x + c.wid, c.y + c.tall, c.z + c.len);
    V2 btr = project(c.x + c.wid, c.y + c.tall, c.z);
    V2 ftl = project(c.x, c.y + c.tall, c.z + c.len);
    
    quad4(bfl, project(c.x + c.wid, c.y, c.z), btr, project(c.x, c.y + c.tall, c.z));
    col(0.4f, 0.4f, 0.4f);
    quad4(ftl, ftr, btr, project(c.x, c.y + c.tall, c.z));
    
    // Headlights
    col(1, 1, 0.8f);
    glPointSize(4.0f);
    glBegin(GL_POINTS);
        V2 l1 = project(c.x + 5, c.y + 5, c.forward ? c.z + c.len : c.z);
        glVertex2f(l1.x, l1.y);
    glEnd();
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------
void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Sky
    glBegin(GL_QUADS);
        col(0.02f, 0.0f, 0.05f); glVertex2f(0, SH);
        col(0.02f, 0.0f, 0.05f); glVertex2f(SW, SH);
        col(0.05f, 0.02f, 0.1f); glVertex2f(SW, 0);
        col(0.05f, 0.02f, 0.1f); glVertex2f(0, 0);
    glEnd();

    // Ground Plane
    col(0.01f, 0.01f, 0.02f);
    V2 g1 = project(-2000, 0, -2000);
    V2 g2 = project(2000, 0, -2000);
    V2 g3 = project(2000, 0, 2000);
    V2 g4 = project(-2000, 0, 2000);
    quad4(g1, g2, g3, g4);

    // Road
    col(0.05f, 0.05f, 0.07f);
    quad4(project(-150, 1, -2000), project(150, 1, -2000), project(150, 1, 2000), project(-150, 1, 2000));

    // Sort buildings and cars together by (X + Z) for correct Isometric depth
    // This is the "Painter's Algorithm"
    std::sort(buildings.begin(), buildings.end(), [](const Building& a, const Building& b) {
        return (a.x + a.z) < (b.x + b.z);
    });

    for (auto& b : buildings) drawBuilding(b);
    for (auto& c : cars) drawCar(c);

    glutSwapBuffers();
}

void timer(int) {
    static float time = 0;
    time += 0.02f;

    for (auto& b : buildings) {
        b.signFlicker = (sin(time * 10 + b.signPhase) > 0) ? 1.0f : 0.2f;
    }

    for (auto& c : cars) {
        c.z += c.forward ? c.speed : -c.speed;
        if (c.z > 1500) c.z = -1500;
        if (c.z < -1500) c.z = 1500;

        c.trail.push_front({c.x + c.wid/2, c.y + 5, c.z});
        if (c.trail.size() > 15) c.trail.pop_back();
    }

    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

int main(int argc, char** argv) {
    srand(time(NULL));
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(SW, SH);
    glutCreateWindow("Isometric Cyberpunk");

    gluOrtho2D(0, SW, 0, SH);
    
    makeStars();
    for (int i = 0; i < 15; ++i) spawnBuilding(true);
    for (int i = 0; i < 15; ++i) spawnBuilding(false);
    for (int i = 0; i < 4; ++i) spawnCar(i);

    glutDisplayFunc(display);
    glutTimerFunc(0, timer, 0);
    glutMainLoop();
    return 0;
}