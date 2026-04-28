/*
    CYBERPUNK METROPOLIS - ISOMETRIC EDITION
    ========================================
    Linear isometric projection with layered city scene.
*/

#ifdef __APPLE__
  #include <GLUT/glut.h>
#else
  #include <GL/glut.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <vector>

static const int SW = 1920;
static const int SH = 1080;

struct V3 { float x, y, z; };

inline float randF(float a, float b) {
    return a + (b - a) * (rand() / float(RAND_MAX));
}

inline V3 project(float x, float y, float z) {
    float sx = (x - z) * 0.866f + (SW * 0.5f);
    float sy = (x + z) * 0.500f + y + (SH * 0.3f);
    float sz = -(x + z) * 0.01f;
    return { sx, sy, sz };
}

inline void screenToWorldGround(float sx, float sy, float& x, float& z) {
    float screenDiagonal = (sx - (SW * 0.5f)) / 0.866f;
    float worldSum = 2.0f * (sy - (SH * 0.3f));
    x = 0.5f * (screenDiagonal + worldSum);
    z = 0.5f * (worldSum - screenDiagonal);
}

inline void col(float r, float g, float b, float a = 1.f) {
    glColor4f(r, g, b, a);
}

struct Building {
    float x, y, z;
    float w, h, d;
    int windowType;
    bool cyanTrim;
};

struct Car {
    float x, y, z;
    float speed;
    bool forward;
    float len, wid, tall;
};

struct Pedestrian {
    float x, y, z;
    float dirX, dirZ;  // Direction vector (normalized)
    float speed;
    float animPhase;   // Animation cycle (0-1)
    float jacketR, jacketG, jacketB;
    float pantsR, pantsG, pantsB;
    float skinR, skinG, skinB;
};

static std::vector<Building> buildings;
static std::vector<Car> cars;
static std::vector<Pedestrian> pedestrians;

static float cursorSX = SW * 0.5f;
static float cursorSY = SH * 0.5f;
static float cursorWX = 0.0f;
static float cursorWZ = 0.0f;
static bool hasPinnedPoint = false;
static float pinnedSX = 0.0f;
static float pinnedSY = 0.0f;
static float pinnedWX = 0.0f;
static float pinnedWZ = 0.0f;

bool isInsideBuildingZone(const Building& b) {
    float minX = b.x;
    float maxX = b.x + b.w;
    float maxZ = b.z + b.d;

    // Keep towers above-road and away from foreground/road corridor.
    if (minX < 260.0f) return false;
    if (maxX > 1880.0f) return false;
    if (b.z < -1320.0f) return false;
    if (maxZ > 1220.0f) return false;
    return true;
}

bool canPlaceBuildingAt(const Building& candidate) {
    if (!isInsideBuildingZone(candidate)) {
        return false;
    }

    const float separation = 10.0f;
    for (const auto& existing : buildings) {
        float cMinX = candidate.x - separation;
        float cMaxX = candidate.x + candidate.w + separation;
        float cMinZ = candidate.z - separation;
        float cMaxZ = candidate.z + candidate.d + separation;

        float eMinX = existing.x - separation;
        float eMaxX = existing.x + existing.w + separation;
        float eMinZ = existing.z - separation;
        float eMaxZ = existing.z + existing.d + separation;

        bool overlapsX = (cMinX < eMaxX) && (cMaxX > eMinX);
        bool overlapsZ = (cMinZ < eMaxZ) && (cMaxZ > eMinZ);
        if (overlapsX && overlapsZ) {
            return false;
        }
    }
    return true;
}

bool tryPlaceBuilding(Building& building) {
    if (canPlaceBuildingAt(building)) {
        return true;
    }

    const float step = 20.0f;
    const float maxOffset = 260.0f;

    for (float offset = step; offset <= maxOffset; offset += step) {
        for (float dx = -offset; dx <= offset; dx += offset) {
            for (float dz = -offset; dz <= offset; dz += offset) {
                if (std::abs(dx) != offset && std::abs(dz) != offset) {
                    continue;
                }

                Building candidate = building;
                candidate.x += dx;
                candidate.z += dz;

                if (canPlaceBuildingAt(candidate)) {
                    building = candidate;
                    return true;
                }
            }
        }
    }

    return false;
}

bool spawnBuilding(float x, float z, float w, float h, float d, int windowType, bool cyanTrim) {
    Building b;
    b.x = x;
    b.y = 0;
    b.z = z;
    b.w = w;
    b.h = h;
    b.d = d;
    b.windowType = windowType;
    b.cyanTrim = cyanTrim;

    if (!tryPlaceBuilding(b)) return false;

    buildings.push_back(b);
    return true;
}

bool spawnBuildingAtScreen(float sx, float sy, float w, float h, float d, int windowType, bool cyanTrim) {
    float x = 0.0f;
    float z = 0.0f;
    screenToWorldGround(sx, sy, x, z);
    return spawnBuilding(x, z, w, h, d, windowType, cyanTrim);
}

void spawnCar(int lane) {
    static const float laneCenters[4] = { -105.0f, -45.0f, 45.0f, 105.0f };
    Car c;
    c.x = laneCenters[(lane >= 0 && lane < 4) ? lane : 0];
    c.y = 0;
    c.forward = (lane > 1);
    c.z = c.forward ? -1200.0f : 1200.0f;
    c.speed = randF(10.0f, 20.0f);
    c.len = 60.0f;
    c.wid = 30.0f;
    c.tall = 20.0f;
    cars.push_back(c);
}

// Forward declarations
void quad4(V3 a, V3 b, V3 c, V3 d);

bool isOnRoad(float x, float z) {
    // Road lanes are at y=0 in middle area; avoid range -105 to 105 in X
    if (x >= -115.0f && x <= 115.0f) {
        return true;  // Central road corridor
    }
    return false;
}

bool canSpawnPedestrianAt(float x, float z) {
    // Can't spawn on road
    if (isOnRoad(x, z)) return false;
    
    // Must be in one of the walking zones
    bool inLeftZone = (x >= 250.0f && x <= 300.0f);  // Left pedestrian zone
    bool inRightZone = (x >= 1840.0f && x <= 1890.0f); // Right pedestrian zone
    bool inMiddleZone = (x >= 400.0f && x <= 1740.0f); // Middle safe zone
    
    if (!inLeftZone && !inRightZone && !inMiddleZone) return false;
    
    // Can't spawn too close to buildings (stricter clearance)
    const float buildingClearance = 80.0f;  // Increased from 60
    for (const auto& b : buildings) {
        if (x > b.x - buildingClearance && x < b.x + b.w + buildingClearance &&
            z > b.z - buildingClearance && z < b.z + b.d + buildingClearance) {
            return false;
        }
    }
    
    // Must be within playable bounds
    if (z < -1280.0f || z > 920.0f) return false;
    
    return true;
}

void spawnPedestrian() {
    if (pedestrians.size() >= 100) return;  // Max 100 pedestrians
    
    Pedestrian p;
    
    // Spawn location: 80% near road edges, 20% in middle areas
    int attempts = 0;
    bool nearRoad = (rand() % 100) < 80;
    
    do {
        if (nearRoad) {
            // Spawn near road edges (left: 250-290, right: 1850-1890)
            if (rand() % 2 == 0) {
                // Left side near road
                p.x = randF(250.0f, 300.0f);
            } else {
                // Right side near road
                p.x = randF(1840.0f, 1890.0f);
            }
            p.z = randF(-1280.0f, 920.0f);
        } else {
            // Spawn in middle areas
            p.x = randF(400.0f, 1740.0f);
            p.z = randF(-1280.0f, 920.0f);
        }
        attempts++;
    } while (!canSpawnPedestrianAt(p.x, p.z) && attempts < 15);
    
    if (attempts >= 15) return;  // Failed to find spot
    
    p.y = 0.0f;
    
    // Direction: if spawning near road, walk parallel (along z-axis)
    if (nearRoad) {
        p.dirX = 0.0f;
        p.dirZ = (rand() % 2 == 0) ? 1.0f : -1.0f;  // Up or down the map
    } else {
        // Random direction for middle spawns
        float angle = randF(0.0f, 6.28f);
        p.dirX = std::cos(angle);
        p.dirZ = std::sin(angle);
    }
    
    p.speed = randF(0.5f, 1.5f);
    p.animPhase = randF(0.0f, 1.0f);
    
    // Random clothing colors
    p.jacketR = randF(0.2f, 0.9f);
    p.jacketG = randF(0.2f, 0.85f);
    p.jacketB = randF(0.2f, 0.9f);
    
    p.pantsR = randF(0.1f, 0.7f);
    p.pantsG = randF(0.1f, 0.7f);
    p.pantsB = randF(0.15f, 0.8f);
    
    p.skinR = 0.92f;
    p.skinG = 0.85f;
    p.skinB = 0.78f;
    
    pedestrians.push_back(p);
}

void drawPedestrian(const Pedestrian& p) {
    // Head (small quad at top of body, ~4 units)
    col(p.skinR, p.skinG, p.skinB);
    quad4(project(p.x - 2.0f, p.y + 16.0f, p.z),
          project(p.x + 2.0f, p.y + 16.0f, p.z),
          project(p.x + 2.0f, p.y + 20.0f, p.z + 2.0f),
          project(p.x - 2.0f, p.y + 20.0f, p.z + 2.0f));
    
    // Torso/jacket (main body ~6 units tall, 3 units wide)
    col(p.jacketR, p.jacketG, p.jacketB);
    float torsoH = 8.0f;
    quad4(project(p.x - 1.5f, p.y + 8.0f, p.z),
          project(p.x + 1.5f, p.y + 8.0f, p.z),
          project(p.x + 1.5f, p.y + 8.0f + torsoH, p.z + 2.0f),
          project(p.x - 1.5f, p.y + 8.0f + torsoH, p.z + 2.0f));
    
    // Walking animation: leg phases (sine wave for natural motion)
    float legSwing = std::sin(p.animPhase * 6.28f);  // -1 to 1
    float armSwing = std::sin(p.animPhase * 6.28f);  // Opposite arm motion
    
    // Left leg (animated)
    col(p.pantsR, p.pantsG, p.pantsB);
    float leftLegSwing = std::sin((p.animPhase + 0.0f) * 6.28f) * 2.0f;  // -2 to 2 unit swing
    quad4(project(p.x - 0.8f, p.y + 8.0f, p.z),
          project(p.x - 2.0f, p.y + 8.0f + leftLegSwing, p.z + 3.0f),
          project(p.x - 1.8f, p.y + 0.0f, p.z + 3.5f),
          project(p.x - 0.6f, p.y, p.z + 0.5f));
    
    // Right leg (animated, opposite phase)
    float rightLegSwing = std::sin((p.animPhase + 0.5f) * 6.28f) * 2.0f;
    quad4(project(p.x + 0.8f, p.y + 8.0f, p.z),
          project(p.x + 2.0f, p.y + 8.0f + rightLegSwing, p.z + 3.0f),
          project(p.x + 1.8f, p.y + 0.0f, p.z + 3.5f),
          project(p.x + 0.6f, p.y, p.z + 0.5f));
    
    // Left arm swinging
    col(p.skinR * 0.95f, p.skinG * 0.95f, p.skinB * 0.95f);
    float leftArmSwing = std::sin((p.animPhase + 0.0f) * 6.28f) * 2.5f;
    glLineWidth(2.0f);
    glBegin(GL_LINES);
        V3 shoulderL = project(p.x - 1.8f, p.y + 14.0f, p.z + 1.0f);
        V3 elbowL = project(p.x - 2.5f, p.y + 12.0f + leftArmSwing, p.z + 2.5f);
        glVertex3f(shoulderL.x, shoulderL.y, shoulderL.z);
        glVertex3f(elbowL.x, elbowL.y, elbowL.z);
    glEnd();
    
    // Right arm swinging (opposite)
    float rightArmSwing = std::sin((p.animPhase + 0.5f) * 6.28f) * 2.5f;
    glBegin(GL_LINES);
        V3 shoulderR = project(p.x + 1.8f, p.y + 14.0f, p.z + 1.0f);
        V3 elbowR = project(p.x + 2.5f, p.y + 12.0f + rightArmSwing, p.z + 2.5f);
        glVertex3f(shoulderR.x, shoulderR.y, shoulderR.z);
        glVertex3f(elbowR.x, elbowR.y, elbowR.z);
    glEnd();
    glLineWidth(1.0f);
}

void updatePedestrian(Pedestrian& p, float dt) {
    // Check if in road-adjacent zones (must walk parallel to road)
    bool inLeftRoadZone = (p.x >= 250.0f && p.x <= 300.0f);   // Left pedestrian zone
    bool inRightRoadZone = (p.x >= 1840.0f && p.x <= 1890.0f); // Right pedestrian zone
    
    if (inLeftRoadZone || inRightRoadZone) {
        // ENFORCE: Walk parallel to road (Z-axis only, no X movement)
        p.dirX = 0.0f;  // Lock X direction
        p.dirZ = (p.dirZ > 0.0f) ? 1.0f : -1.0f;  // Ensure walking up or down
    } else {
        // In middle areas: active collision avoidance
        float steerX = 0.0f, steerZ = 0.0f;
        const float buildingDetectRadius = 150.0f;  // Increased detection radius
        
        // Strong building avoidance
        for (const auto& b : buildings) {
            float bCenterX = b.x + b.w * 0.5f;
            float bCenterZ = b.z + b.d * 0.5f;
            float distX = p.x - bCenterX;
            float distZ = p.z - bCenterZ;
            float dist = std::sqrt(distX * distX + distZ * distZ);
            
            if (dist < buildingDetectRadius && dist > 0.1f) {
                // Strong repulsion force
                float force = 1.5f - (dist / buildingDetectRadius);  // Increased force
                if (force > 0.0f) {
                    steerX += (distX / dist) * force;
                    steerZ += (distZ / dist) * force;
                }
            }
        }
        
        // Strong road avoidance (keep away from central road)
        if (p.x >= -115.0f && p.x <= 115.0f) {
            // INSIDE road - push away strongly
            if (p.x < 0.0f) {
                steerX -= 2.5f;  // Push left
            } else {
                steerX += 2.5f;  // Push right
            }
        } else if (p.x > 115.0f && p.x < 250.0f) {
            // Between road and left zone - push toward left zone
            steerX -= 1.5f;  // Push left
        } else if (p.x > 1890.0f && p.x < 2000.0f) {
            // Between right zone and road - push toward right zone
            steerX += 1.5f;  // Push right
        }
        
        // Normalize and blend steering
        if (steerX != 0.0f || steerZ != 0.0f) {
            float steerLen = std::sqrt(steerX * steerX + steerZ * steerZ);
            if (steerLen > 0.1f) {
                steerX /= steerLen;
                steerZ /= steerLen;
                // Higher steering weight for strong collision avoidance
                p.dirX = p.dirX * 0.30f + steerX * 0.70f;  // More avoidance (was 80/20)
                p.dirZ = p.dirZ * 0.30f + steerZ * 0.70f;
                
                float dirLen = std::sqrt(p.dirX * p.dirX + p.dirZ * p.dirZ);
                if (dirLen > 0.1f) {
                    p.dirX /= dirLen;
                    p.dirZ /= dirLen;
                }
            }
        }
    }
    
    // Movement
    p.x += p.dirX * p.speed;
    p.z += p.dirZ * p.speed;
    
    // Animation phase
    p.animPhase += dt * 0.016f;
    if (p.animPhase > 1.0f) p.animPhase -= 1.0f;
    
    // Only remove if they go out of z-axis bounds (allow indefinite x movement along road)
    if (p.z < -1350.0f || p.z > 1000.0f) {
        // Will be removed in timer loop
    }
}

void quad4(V3 a, V3 b, V3 c, V3 d) {
    glBegin(GL_QUADS);
        glVertex3f(a.x, a.y, a.z);
        glVertex3f(b.x, b.y, b.z);
        glVertex3f(c.x, c.y, c.z);
        glVertex3f(d.x, d.y, d.z);
    glEnd();
}

void drawText2D(float x, float y, const char* text, float r, float g, float b, float a = 1.0f) {
    col(r, g, b, a);
    glRasterPos2f(x, y);
    for (const char* p = text; *p; ++p) {
        glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *p);
    }
}

void updateCursorFromScreen(float sx, float sy) {
    cursorSX = sx;
    cursorSY = sy;
    screenToWorldGround(cursorSX, cursorSY, cursorWX, cursorWZ);
}

void onMouseMove(int x, int y) {
    updateCursorFromScreen(static_cast<float>(x), static_cast<float>(SH - y - 69.0f));
    glutPostRedisplay();
}

void onMouseClick(int button, int state, int x, int y) {
    if (button != GLUT_LEFT_BUTTON || state != GLUT_DOWN) {
        return;
    }

    updateCursorFromScreen(static_cast<float>(x), static_cast<float>(SH - y - 69.0f));
    hasPinnedPoint = true;
    pinnedSX = cursorSX;
    pinnedSY = cursorSY;
    pinnedWX = cursorWX;
    pinnedWZ = cursorWZ;

    std::printf("PIN screen=(%.1f, %.1f) world=(%.1f, %.1f)\n", pinnedSX, pinnedSY, pinnedWX, pinnedWZ);
    std::fflush(stdout);
}

void drawStreetlight(float baseX, float baseZ, bool fromLeftSide) {
    float baseY = 1.2f;
    float poleHeight = 95.0f;
    float armReach = 70.0f;
    float tipX = fromLeftSide ? (baseX + armReach) : (baseX - armReach);

    V3 base = project(baseX, baseY, baseZ);
    V3 top = project(baseX, baseY + poleHeight, baseZ);
    V3 tip = project(tipX, baseY + poleHeight, baseZ);

    col(0.45f, 0.45f, 0.5f);
    glLineWidth(5.0f);
    glBegin(GL_LINES);
        glVertex3f(base.x, base.y, base.z);
        glVertex3f(top.x, top.y, top.z);
        glVertex3f(top.x, top.y, top.z);
        glVertex3f(tip.x, tip.y, tip.z);
    glEnd();

    col(1.0f, 0.95f, 0.6f, 0.9f);
    glPointSize(4.0f);
    glBegin(GL_POINTS);
        glVertex3f(tip.x, tip.y, tip.z);
    glEnd();
    glLineWidth(1.0f);
}

void drawDumpster(float x, float z) {
    float y = 1.2f;
    float w = 34.0f;
    float d = 22.0f;
    float h = 16.0f;

    col(0.08f, 0.35f, 0.12f);
    quad4(project(x, y, z), project(x + w, y, z), project(x + w, y + h, z), project(x, y + h, z));
    quad4(project(x, y, z), project(x, y, z + d), project(x, y + h, z + d), project(x, y + h, z));

    col(0.10f, 0.45f, 0.16f);
    quad4(project(x - 1.5f, y + h, z - 1.5f),
          project(x + w + 1.5f, y + h, z - 1.5f),
          project(x + w + 1.5f, y + h + 2.2f, z + d + 1.5f),
          project(x - 1.5f, y + h + 2.2f, z + d + 1.5f));

    col(0.05f, 0.05f, 0.05f);
    glPointSize(2.8f);
    glBegin(GL_POINTS);
        V3 w1 = project(x + 5, y, z + 2);
        V3 w2 = project(x + w - 5, y, z + 2);
        V3 w3 = project(x + 5, y, z + d - 2);
        V3 w4 = project(x + w - 5, y, z + d - 2);
        glVertex3f(w1.x, w1.y, w1.z);
        glVertex3f(w2.x, w2.y, w2.z);
        glVertex3f(w3.x, w3.y, w3.z);
        glVertex3f(w4.x, w4.y, w4.z);
    glEnd();
}

void drawPathway(float x1, float z1, float x2, float z2, float y, float width, float r, float g, float b, float a) {
    float dx = x2 - x1;
    float dz = z2 - z1;
    float len = std::sqrt(dx * dx + dz * dz);
    if (len < 0.001f) return;

    float nx = -dz / len * (width * 0.5f);
    float nz = dx / len * (width * 0.5f);

    col(r, g, b, a);
    quad4(
        project(x1 - nx, y, z1 - nz),
        project(x1 + nx, y, z1 + nz),
        project(x2 + nx, y, z2 + nz),
        project(x2 - nx, y, z2 - nz)
    );
}

void drawBench(float x, float z, float w, float d, float seatY, float backH) {
    col(0.44f, 0.29f, 0.16f);
    quad4(project(x, seatY, z), project(x + w, seatY, z), project(x + w, seatY, z + d), project(x, seatY, z + d));

    col(0.38f, 0.24f, 0.13f);
    quad4(project(x, seatY, z + d), project(x + w, seatY, z + d), project(x + w, seatY + backH, z + d), project(x, seatY + backH, z + d));

    col(0.20f, 0.20f, 0.22f);
    glLineWidth(3.0f);
    glBegin(GL_LINES);
        V3 l1b = project(x + 2.0f, 0.9f, z + 2.0f);      V3 l1t = project(x + 2.0f, seatY, z + 2.0f);
        V3 l2b = project(x + w - 2.0f, 0.9f, z + 2.0f);  V3 l2t = project(x + w - 2.0f, seatY, z + 2.0f);
        V3 l3b = project(x + 2.0f, 0.9f, z + d - 2.0f);  V3 l3t = project(x + 2.0f, seatY, z + d - 2.0f);
        V3 l4b = project(x + w - 2.0f, 0.9f, z + d - 2.0f); V3 l4t = project(x + w - 2.0f, seatY, z + d - 2.0f);
        glVertex3f(l1b.x, l1b.y, l1b.z); glVertex3f(l1t.x, l1t.y, l1t.z);
        glVertex3f(l2b.x, l2b.y, l2b.z); glVertex3f(l2t.x, l2t.y, l2t.z);
        glVertex3f(l3b.x, l3b.y, l3b.z); glVertex3f(l3t.x, l3t.y, l3t.z);
        glVertex3f(l4b.x, l4b.y, l4b.z); glVertex3f(l4t.x, l4t.y, l4t.z);
    glEnd();
    glLineWidth(1.0f);
}

void drawBenchAlongZ(float x, float z, float depthX, float lenZ, float seatY, float backH, bool backOnWestSide) {
    col(0.44f, 0.29f, 0.16f);
    quad4(project(x, seatY, z), project(x + depthX, seatY, z), project(x + depthX, seatY, z + lenZ), project(x, seatY, z + lenZ));

    col(0.38f, 0.24f, 0.13f);
    float backX = backOnWestSide ? x : (x + depthX);
    quad4(project(backX, seatY, z), project(backX, seatY, z + lenZ), project(backX, seatY + backH, z + lenZ), project(backX, seatY + backH, z));

    col(0.20f, 0.20f, 0.22f);
    glLineWidth(3.0f);
    glBegin(GL_LINES);
        float x1 = x + 1.7f;
        float x2 = x + depthX - 1.7f;
        float z1 = z + 1.8f;
        float z2 = z + lenZ - 1.8f;
        V3 l1b = project(x1, 0.9f, z1); V3 l1t = project(x1, seatY, z1);
        V3 l2b = project(x2, 0.9f, z1); V3 l2t = project(x2, seatY, z1);
        V3 l3b = project(x1, 0.9f, z2); V3 l3t = project(x1, seatY, z2);
        V3 l4b = project(x2, 0.9f, z2); V3 l4t = project(x2, seatY, z2);
        glVertex3f(l1b.x, l1b.y, l1b.z); glVertex3f(l1t.x, l1t.y, l1t.z);
        glVertex3f(l2b.x, l2b.y, l2b.z); glVertex3f(l2t.x, l2t.y, l2t.z);
        glVertex3f(l3b.x, l3b.y, l3b.z); glVertex3f(l3t.x, l3t.y, l3t.z);
        glVertex3f(l4b.x, l4b.y, l4b.z); glVertex3f(l4t.x, l4t.y, l4t.z);
    glEnd();
    glLineWidth(1.0f);
}

void drawTopBenchArea() {
    const float benchHubX = 295.0f;
    const float benchHubZ = 675.0f;

    const float half = 58.0f;
    const float inset = 8.0f;
    const float seatY = 7.8f;
    const float backH = 12.0f;

    col(0.12f, 0.12f, 0.14f, 0.92f);
    quad4(project(benchHubX - half, 1.0f, benchHubZ - half), project(benchHubX + half, 1.0f, benchHubZ - half), project(benchHubX + half, 1.0f, benchHubZ + half), project(benchHubX - half, 1.0f, benchHubZ + half));

    col(0.40f, 0.92f, 1.0f, 0.10f);
    quad4(project(benchHubX - half + inset, 1.2f, benchHubZ - half + inset), project(benchHubX + half - inset, 1.2f, benchHubZ - half + inset), project(benchHubX + half - inset, 1.2f, benchHubZ + half - inset), project(benchHubX - half + inset, 1.2f, benchHubZ + half - inset));

    // North side benches facing center (smaller and pushed inward).
    drawBench(benchHubX - 32.0f, benchHubZ + half - 26.0f, 18.0f, 5.0f, seatY, backH);
    drawBench(benchHubX - 8.0f, benchHubZ + half - 26.0f, 18.0f, 5.0f, seatY, backH);
    drawBench(benchHubX + 16.0f, benchHubZ + half - 26.0f, 18.0f, 5.0f, seatY, backH);

    // West side benches facing center (smaller and pushed inward).
    drawBenchAlongZ(benchHubX - half + 17.0f, benchHubZ - 32.0f, 5.0f, 18.0f, seatY, backH, true);
    drawBenchAlongZ(benchHubX - half + 17.0f, benchHubZ - 8.0f, 5.0f, 18.0f, seatY, backH, true);
    drawBenchAlongZ(benchHubX - half + 17.0f, benchHubZ + 16.0f, 5.0f, 18.0f, seatY, backH, true);

    // East side benches facing center (smaller and pushed inward).
    drawBenchAlongZ(benchHubX + half - 22.0f, benchHubZ - 32.0f, 5.0f, 18.0f, seatY, backH, false);
    drawBenchAlongZ(benchHubX + half - 22.0f, benchHubZ - 8.0f, 5.0f, 18.0f, seatY, backH, false);
    drawBenchAlongZ(benchHubX + half - 22.0f, benchHubZ + 16.0f, 5.0f, 18.0f, seatY, backH, false);
}

void drawConvenienceStore() {
    float storeX = -462.2f;
    float storeZ = -175.8f;

    const float baseY = 6.0f;
    const float sizeX = 250.0f;
    const float sizeZ = 280.0f;
    const float topY = 136.0f;

    // Taller, longer footprint pushed toward user target pin.
    col(0.07f, 0.08f, 0.11f);
    quad4(project(storeX, baseY, storeZ), project(storeX + sizeX, baseY, storeZ), project(storeX + sizeX, topY, storeZ), project(storeX, topY, storeZ));
    quad4(project(storeX, baseY, storeZ), project(storeX, baseY, storeZ + sizeZ), project(storeX, topY, storeZ + sizeZ), project(storeX, topY, storeZ));
    quad4(project(storeX + sizeX, baseY, storeZ), project(storeX + sizeX, baseY, storeZ + sizeZ), project(storeX + sizeX, topY, storeZ + sizeZ), project(storeX + sizeX, topY, storeZ));

    col(0.10f, 0.11f, 0.15f);
    quad4(project(storeX, topY, storeZ), project(storeX + sizeX, topY, storeZ), project(storeX + sizeX, topY, storeZ + sizeZ), project(storeX, topY, storeZ + sizeZ));

    // Full-height glass panes on front and west faces.
    col(0.45f, 0.95f, 1.0f, 0.35f);
    quad4(project(storeX + 8.0f, baseY + 10.0f, storeZ), project(storeX + sizeX - 8.0f, baseY + 10.0f, storeZ), project(storeX + sizeX - 8.0f, topY - 10.0f, storeZ), project(storeX + 8.0f, topY - 10.0f, storeZ));
    quad4(project(storeX, baseY + 10.0f, storeZ + 8.0f), project(storeX, baseY + 10.0f, storeZ + sizeZ - 8.0f), project(storeX, topY - 10.0f, storeZ + sizeZ - 8.0f), project(storeX, topY - 10.0f, storeZ + 8.0f));

    col(0.80f, 0.95f, 1.0f, 0.18f);
    quad4(project(storeX + 15.0f, baseY + 14.0f, storeZ + 2.0f), project(storeX + sizeX - 15.0f, baseY + 14.0f, storeZ + 2.0f), project(storeX + sizeX - 15.0f, topY - 14.0f, storeZ + 2.0f), project(storeX + 15.0f, topY - 14.0f, storeZ + 2.0f));
    quad4(project(storeX + 2.0f, baseY + 14.0f, storeZ + 15.0f), project(storeX + 2.0f, baseY + 14.0f, storeZ + sizeZ - 15.0f), project(storeX + 2.0f, topY - 14.0f, storeZ + sizeZ - 15.0f), project(storeX + 2.0f, topY - 14.0f, storeZ + 15.0f));

    // Split both glass facades into a 4x2 pane grid.
    col(0.82f, 0.95f, 1.0f, 0.45f);
    glLineWidth(1.5f);
    glBegin(GL_LINES);
        float gXMin = storeX + 8.0f;
        float gXMax = storeX + sizeX - 8.0f;
        float gYMin = baseY + 10.0f;
        float gYMax = topY - 10.0f;
        float gX1 = gXMin + (gXMax - gXMin) * 0.25f;
        float gX2 = gXMin + (gXMax - gXMin) * 0.50f;
        float gX3 = gXMin + (gXMax - gXMin) * 0.75f;
        float gYMid = gYMin + (gYMax - gYMin) * 0.50f;

        glVertex3f(project(gX1, gYMin, storeZ).x, project(gX1, gYMin, storeZ).y, project(gX1, gYMin, storeZ).z);
        glVertex3f(project(gX1, gYMax, storeZ).x, project(gX1, gYMax, storeZ).y, project(gX1, gYMax, storeZ).z);
        glVertex3f(project(gX2, gYMin, storeZ).x, project(gX2, gYMin, storeZ).y, project(gX2, gYMin, storeZ).z);
        glVertex3f(project(gX2, gYMax, storeZ).x, project(gX2, gYMax, storeZ).y, project(gX2, gYMax, storeZ).z);
        glVertex3f(project(gX3, gYMin, storeZ).x, project(gX3, gYMin, storeZ).y, project(gX3, gYMin, storeZ).z);
        glVertex3f(project(gX3, gYMax, storeZ).x, project(gX3, gYMax, storeZ).y, project(gX3, gYMax, storeZ).z);
        glVertex3f(project(gXMin, gYMid, storeZ).x, project(gXMin, gYMid, storeZ).y, project(gXMin, gYMid, storeZ).z);
        glVertex3f(project(gXMax, gYMid, storeZ).x, project(gXMax, gYMid, storeZ).y, project(gXMax, gYMid, storeZ).z);

        float gZMin = storeZ + 8.0f;
        float gZMax = storeZ + sizeZ - 8.0f;
        float gZ1 = gZMin + (gZMax - gZMin) * 0.25f;
        float gZ2 = gZMin + (gZMax - gZMin) * 0.50f;
        float gZ3 = gZMin + (gZMax - gZMin) * 0.75f;
        glVertex3f(project(storeX, gYMin, gZ1).x, project(storeX, gYMin, gZ1).y, project(storeX, gYMin, gZ1).z);
        glVertex3f(project(storeX, gYMax, gZ1).x, project(storeX, gYMax, gZ1).y, project(storeX, gYMax, gZ1).z);
        glVertex3f(project(storeX, gYMin, gZ2).x, project(storeX, gYMin, gZ2).y, project(storeX, gYMin, gZ2).z);
        glVertex3f(project(storeX, gYMax, gZ2).x, project(storeX, gYMax, gZ2).y, project(storeX, gYMax, gZ2).z);
        glVertex3f(project(storeX, gYMin, gZ3).x, project(storeX, gYMin, gZ3).y, project(storeX, gYMin, gZ3).z);
        glVertex3f(project(storeX, gYMax, gZ3).x, project(storeX, gYMax, gZ3).y, project(storeX, gYMax, gZ3).z);
        glVertex3f(project(storeX, gYMid, gZMin).x, project(storeX, gYMid, gZMin).y, project(storeX, gYMid, gZMin).z);
        glVertex3f(project(storeX, gYMid, gZMax).x, project(storeX, gYMid, gZMax).y, project(storeX, gYMid, gZMax).z);
    glEnd();
    glLineWidth(1.0f);

    // Entrance and accent sign.
    col(0.95f, 0.95f, 1.0f, 0.9f);
    quad4(project(storeX + 88.0f, baseY + 6.0f, storeZ), project(storeX + 138.0f, baseY + 6.0f, storeZ), project(storeX + 138.0f, baseY + 62.0f, storeZ), project(storeX + 88.0f, baseY + 62.0f, storeZ));

    // Low-opacity grey path coming out from the entrance.
    col(0.62f, 0.62f, 0.64f, 0.18f);
        quad4(project(storeX + 94.0f, 1.05f, storeZ - 72.0f),
            project(storeX + 132.0f, 1.05f, storeZ - 72.0f),
            project(storeX + 132.0f, 1.05f, storeZ),
            project(storeX + 94.0f, 1.05f, storeZ));

    col(0.96f, 0.84f, 0.30f);
    quad4(project(storeX + 52.0f, topY + 3.0f, storeZ + 12.0f), project(storeX + 194.0f, topY + 3.0f, storeZ + 12.0f), project(storeX + 198.0f, topY + 15.0f, storeZ + 40.0f), project(storeX + 56.0f, topY + 15.0f, storeZ + 40.0f));
}

void drawGasStation() {
    col(0.08f, 0.08f, 0.10f);
    quad4(project(-785, 6.0f, 290), project(-200, 6.0f, 290), project(-200, 6.0f, 850), project(-785, 6.0f, 850));

    col(0.12f, 0.12f, 0.15f);
    quad4(project(-730, 8.5f, 350), project(-255, 8.5f, 350), project(-255, 8.5f, 770), project(-730, 8.5f, 770));

    col(0.18f, 0.18f, 0.22f);
    quad4(project(-690, 58.0f, 390), project(-300, 58.0f, 390), project(-300, 58.0f, 710), project(-690, 58.0f, 710));

    col(0.55f, 0.55f, 0.60f);
    glLineWidth(5.0f);
    glBegin(GL_LINES);
        V3 p1b = project(-650, 8.5f, 420); V3 p1t = project(-650, 58.0f, 420);
        V3 p2b = project(-340, 8.5f, 420); V3 p2t = project(-340, 58.0f, 420);
        V3 p3b = project(-650, 8.5f, 675); V3 p3t = project(-650, 58.0f, 675);
        V3 p4b = project(-340, 8.5f, 675); V3 p4t = project(-340, 58.0f, 675);
        glVertex3f(p1b.x, p1b.y, p1b.z); glVertex3f(p1t.x, p1t.y, p1t.z);
        glVertex3f(p2b.x, p2b.y, p2b.z); glVertex3f(p2t.x, p2t.y, p2t.z);
        glVertex3f(p3b.x, p3b.y, p3b.z); glVertex3f(p3t.x, p3t.y, p3t.z);
        glVertex3f(p4b.x, p4b.y, p4b.z); glVertex3f(p4t.x, p4t.y, p4t.z);
    glEnd();
    glLineWidth(1.0f);

    col(0.22f, 0.22f, 0.26f);
    quad4(project(-610, 8.5f, 565), project(-470, 8.5f, 565), project(-470, 8.5f, 730), project(-610, 8.5f, 730));

    col(0.78f, 0.78f, 0.82f);
    quad4(project(-600, 8.5f, 450), project(-560, 8.5f, 450), project(-560, 33, 450), project(-600, 33, 450));
    quad4(project(-535, 8.5f, 450), project(-495, 8.5f, 450), project(-495, 33, 450), project(-535, 33, 450));
    quad4(project(-470, 8.5f, 450), project(-430, 8.5f, 450), project(-430, 33, 450), project(-470, 33, 450));

    col(0.98f, 0.98f, 1.0f);
    quad4(project(-598, 18, 450), project(-562, 18, 450), project(-562, 28, 450), project(-598, 28, 450));
    quad4(project(-533, 18, 450), project(-497, 18, 450), project(-497, 28, 450), project(-533, 28, 450));
    quad4(project(-468, 18, 450), project(-432, 18, 450), project(-432, 28, 450), project(-468, 28, 450));

    col(0.22f, 0.22f, 0.25f);
    quad4(project(-640, 8.5f, 600), project(-610, 8.5f, 600), project(-610, 28, 600), project(-640, 28, 600));
    quad4(project(-560, 8.5f, 600), project(-530, 8.5f, 600), project(-530, 28, 600), project(-560, 28, 600));
    quad4(project(-480, 8.5f, 600), project(-450, 8.5f, 600), project(-450, 28, 600), project(-480, 28, 600));
}

void drawBuildingWindows(const Building& b) {
    float windowSpacingX = 25.0f;
    float windowSpacingY = 30.0f;
    float windowSizeX = 12.0f;
    float windowSizeY = 15.0f;
    int sideCols = 3;

    if (b.windowType == 2) {
        windowSpacingX = 40.0f;
        windowSpacingY = 48.0f;
        windowSizeX = 8.0f;
        windowSizeY = 24.0f;
        sideCols = 2;
    }

    if (b.windowType == 3) {
        windowSpacingY = 58.0f;
        float insetX = b.w * 0.05f;
        float insetZ = b.d * 0.05f;
        float bandH = 9.0f;
        int bands = (int)(b.h / windowSpacingY);
        if (bands < 1) bands = 1;

        for (int row = 0; row < bands; ++row) {
            float cy = b.y + (row + 0.5f) * windowSpacingY;
            float x1 = b.x + insetX;
            float x2 = b.x + b.w - insetX;

            // Vary color for type 3 windows with maximum local variation
            int colorIdx = (int)((row * 73.0f + b.x * 3.0f + b.z * 5.0f)) % 10;
            float cr, cg, cb;
            if (colorIdx < 3) {
                cr = 0.95f; cg = 0.70f; cb = 0.30f;  // Warm amber
            } else if (colorIdx < 6) {
                cr = 0.25f; cg = 0.70f; cb = 0.95f;  // Cool blue
            } else if (colorIdx < 8) {
                cr = 0.35f; cg = 0.85f; cb = 0.90f;  // Cool cyan
            } else {
                cr = 0.08f; cg = 0.08f; cb = 0.10f;  // Dark/off
            }
            col(cr, cg, cb, colorIdx < 8 ? 0.75f : 0.15f);

            V3 wbl = project(x1, cy - bandH * 0.5f, b.z);
            V3 wbr = project(x2, cy - bandH * 0.5f, b.z);
            V3 wtl = project(x1, cy + bandH * 0.5f, b.z);
            V3 wtr = project(x2, cy + bandH * 0.5f, b.z);
            quad4(wbl, wbr, wtr, wtl);

            float z1 = b.z + insetZ;
            float z2 = b.z + b.d - insetZ;
            V3 sbl = project(b.x, cy - bandH * 0.5f, z1);
            V3 sbr = project(b.x, cy - bandH * 0.5f, z2);
            V3 stl = project(b.x, cy + bandH * 0.5f, z1);
            V3 str = project(b.x, cy + bandH * 0.5f, z2);
            quad4(sbl, sbr, str, stl);
        }
        return;
    }

    int windowsPerRow = (int)(b.w / windowSpacingX);
    int windowsPerCol = (int)(b.h / windowSpacingY);
    if (windowsPerRow < 1) windowsPerRow = 1;
    if (windowsPerCol < 1) windowsPerCol = 1;

    // Front face windows with maximally varied colors
    for (int row = 0; row < windowsPerCol; ++row) {
        for (int colIdx = 0; colIdx < windowsPerRow; ++colIdx) {
            // Maximize local variation: row and col are dominant in hash
            int colorIdx = (int)((row * 73.0f + colIdx * 97.0f + b.x * 3.0f + b.z * 5.0f)) % 10;
            float cr, cg, cb;
            if (colorIdx < 3) {
                cr = 0.95f; cg = 0.70f; cb = 0.30f;  // Warm amber
            } else if (colorIdx < 6) {
                cr = 0.25f; cg = 0.70f; cb = 0.95f;  // Cool blue
            } else if (colorIdx < 8) {
                cr = 0.35f; cg = 0.85f; cb = 0.90f;  // Cool cyan
            } else {
                cr = 0.08f; cg = 0.08f; cb = 0.10f;  // Dark/off
            }
            col(cr, cg, cb, colorIdx < 8 ? 0.75f : 0.15f);

            float windowX = b.x + (colIdx + 0.5f) * windowSpacingX;
            float windowY = b.y + (row + 0.5f) * windowSpacingY;
            float windowZ = b.z;
            V3 wbl = project(windowX - windowSizeX * 0.5f, windowY - windowSizeY * 0.5f, windowZ);
            V3 wbr = project(windowX + windowSizeX * 0.5f, windowY - windowSizeY * 0.5f, windowZ);
            V3 wtl = project(windowX - windowSizeX * 0.5f, windowY + windowSizeY * 0.5f, windowZ);
            V3 wtr = project(windowX + windowSizeX * 0.5f, windowY + windowSizeY * 0.5f, windowZ);
            quad4(wbl, wbr, wtr, wtl);
        }
    }

    // Side face windows with maximally varied colors
    for (int row = 0; row < windowsPerCol; ++row) {
        for (int colIdx = 0; colIdx < sideCols; ++colIdx) {
            int colorIdx = (int)((row * 73.0f + colIdx * 97.0f + b.x * 3.0f + b.z * 5.0f + 41.0f)) % 10;
            float cr, cg, cb;
            if (colorIdx < 3) {
                cr = 0.95f; cg = 0.70f; cb = 0.30f;  // Warm amber
            } else if (colorIdx < 6) {
                cr = 0.25f; cg = 0.70f; cb = 0.95f;  // Cool blue
            } else if (colorIdx < 8) {
                cr = 0.35f; cg = 0.85f; cb = 0.90f;  // Cool cyan
            } else {
                cr = 0.08f; cg = 0.08f; cb = 0.10f;  // Dark/off
            }
            col(cr, cg, cb, colorIdx < 8 ? 0.75f : 0.15f);

            float windowX = b.x;
            float windowY = b.y + (row + 0.5f) * windowSpacingY;
            float windowZ = b.z + (colIdx + 0.5f) * (b.d / sideCols);
            V3 wbl = project(windowX, windowY - windowSizeY * 0.5f, windowZ - windowSizeX * 0.5f);
            V3 wbr = project(windowX, windowY - windowSizeY * 0.5f, windowZ + windowSizeX * 0.5f);
            V3 wtl = project(windowX, windowY + windowSizeY * 0.5f, windowZ - windowSizeX * 0.5f);
            V3 wtr = project(windowX, windowY + windowSizeY * 0.5f, windowZ + windowSizeX * 0.5f);
            quad4(wbl, wbr, wtr, wtl);
        }
    }
}

void drawBuilding(const Building& b) {
    V3 fbl = project(b.x, b.y, b.z);
    V3 fbr = project(b.x + b.w, b.y, b.z);
    V3 bbl = project(b.x, b.y, b.z + b.d);
    V3 bbr = project(b.x + b.w, b.y, b.z + b.d);
    V3 ftl = project(b.x, b.y + b.h, b.z);
    V3 ftr = project(b.x + b.w, b.y + b.h, b.z);
    V3 btl = project(b.x, b.y + b.h, b.z + b.d);
    V3 btr = project(b.x + b.w, b.y + b.h, b.z + b.d);

    col(0.08f, 0.08f, 0.12f);
    quad4(fbl, bbl, btl, ftl);
    col(0.14f, 0.15f, 0.20f);
    quad4(ftl, ftr, btr, btl);
    col(0.10f, 0.10f, 0.15f);
    quad4(fbl, fbr, ftr, ftl);

    if (b.cyanTrim) col(0.35f, 0.85f, 0.95f, 0.75f);
    else if (b.windowType == 3) col(0.90f, 0.45f, 0.80f, 0.75f);
    else col(0.95f, 0.75f, 0.35f, 0.75f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
        glVertex3f(fbl.x, fbl.y, fbl.z);
        glVertex3f(fbr.x, fbr.y, fbr.z);
        glVertex3f(ftr.x, ftr.y, ftr.z);
        glVertex3f(ftl.x, ftl.y, ftl.z);
    glEnd();
    glBegin(GL_LINE_LOOP);
        glVertex3f(ftl.x, ftl.y, ftl.z);
        glVertex3f(ftr.x, ftr.y, ftr.z);
        glVertex3f(btr.x, btr.y, btr.z);
        glVertex3f(btl.x, btl.y, btl.z);
    glEnd();
    glBegin(GL_LINE_LOOP);
        glVertex3f(fbl.x, fbl.y, fbl.z);
        glVertex3f(bbl.x, bbl.y, bbl.z);
        glVertex3f(btl.x, btl.y, btl.z);
        glVertex3f(ftl.x, ftl.y, ftl.z);
    glEnd();
    glLineWidth(1.0f);

    drawBuildingWindows(b);

    // Extended rooftop details: hvac boxes, solar panels, chimneys, water tank, railings, ladders, antenna.
    float rInsetX = b.w * 0.16f;
    float rInsetZ = b.d * 0.18f;
    float roofY = b.y + b.h;

    // HVAC Box 1 (main unit, south-west corner)
    col(0.34f, 0.35f, 0.38f);
    quad4(project(b.x + rInsetX, roofY + 1.0f, b.z + rInsetZ),
          project(b.x + rInsetX + 26.0f, roofY + 1.0f, b.z + rInsetZ),
          project(b.x + rInsetX + 26.0f, roofY + 16.0f, b.z + rInsetZ + 20.0f),
          project(b.x + rInsetX, roofY + 16.0f, b.z + rInsetZ + 20.0f));
    col(0.46f, 0.47f, 0.50f);
    quad4(project(b.x + rInsetX, roofY + 16.0f, b.z + rInsetZ),
          project(b.x + rInsetX + 26.0f, roofY + 16.0f, b.z + rInsetZ),
          project(b.x + rInsetX + 26.0f, roofY + 16.0f, b.z + rInsetZ + 20.0f),
          project(b.x + rInsetX, roofY + 16.0f, b.z + rInsetZ + 20.0f));

    // HVAC Box 2 (smaller unit, north-east corner)
    col(0.36f, 0.37f, 0.40f);
    quad4(project(b.x + b.w - rInsetX - 20.0f, roofY + 2.0f, b.z + b.d - rInsetZ - 24.0f),
          project(b.x + b.w - rInsetX, roofY + 2.0f, b.z + b.d - rInsetZ - 24.0f),
          project(b.x + b.w - rInsetX, roofY + 14.0f, b.z + b.d - rInsetZ),
          project(b.x + b.w - rInsetX - 20.0f, roofY + 14.0f, b.z + b.d - rInsetZ));

    // Chimney Stack (metal pipe, center-back)
    col(0.42f, 0.43f, 0.46f);
    float chimneyH = 28.0f;
    float chimneyX = b.x + b.w * 0.5f;
    float chimneyZ = b.z + b.d - rInsetZ - 6.0f;
    quad4(project(chimneyX - 5.0f, roofY + 1.0f, chimneyZ),
          project(chimneyX + 5.0f, roofY + 1.0f, chimneyZ),
          project(chimneyX + 5.0f, roofY + chimneyH, chimneyZ + 4.0f),
          project(chimneyX - 5.0f, roofY + chimneyH, chimneyZ + 4.0f));
    col(0.35f, 0.36f, 0.39f);
    quad4(project(chimneyX - 5.0f, roofY + 1.0f, chimneyZ),
          project(chimneyX - 5.0f, roofY + chimneyH, chimneyZ + 4.0f),
          project(chimneyX + 5.0f, roofY + chimneyH, chimneyZ + 4.0f),
          project(chimneyX + 5.0f, roofY + 1.0f, chimneyZ));

    // Water Tank (tall cylindrical look, north corner)
    col(0.40f, 0.41f, 0.44f);
    float tankX = b.x + rInsetX + 10.0f;
    float tankZ = b.z + b.d - rInsetZ - 12.0f;
    float tankH = 35.0f;
    float tankR = 8.0f;
    quad4(project(tankX - tankR, roofY + 2.0f, tankZ),
          project(tankX + tankR, roofY + 2.0f, tankZ),
          project(tankX + tankR, roofY + tankH, tankZ + tankR),
          project(tankX - tankR, roofY + tankH, tankZ + tankR));
    col(0.48f, 0.49f, 0.52f);
    quad4(project(tankX - tankR, roofY + 2.0f, tankZ),
          project(tankX - tankR, roofY + tankH, tankZ + tankR),
          project(tankX + tankR, roofY + tankH, tankZ + tankR),
          project(tankX + tankR, roofY + 2.0f, tankZ));

    // Solar Panels (grid on roof surface, upper area)
    col(0.18f, 0.24f, 0.32f, 0.65f);
    glLineWidth(1.0f);
    float solarStartX = b.x + b.w * 0.15f;
    float solarEndX = b.x + b.w * 0.85f;
    float solarStartZ = b.z + rInsetZ + 8.0f;
    float solarEndZ = b.z + b.d * 0.5f;
    float panelW = (solarEndX - solarStartX) / 3.0f;
    float panelD = (solarEndZ - solarStartZ) / 2.0f;
    
    for (int px = 0; px < 3; ++px) {
        for (int pz = 0; pz < 2; ++pz) {
            float pX0 = solarStartX + px * panelW;
            float pX1 = pX0 + panelW - 2.0f;
            float pZ0 = solarStartZ + pz * panelD;
            float pZ1 = pZ0 + panelD - 2.0f;
            col(0.20f, 0.28f, 0.38f, 0.70f);
            quad4(project(pX0, roofY + 0.5f, pZ0),
                  project(pX1, roofY + 0.5f, pZ0),
                  project(pX1, roofY + 0.5f, pZ1),
                  project(pX0, roofY + 0.5f, pZ1));
            col(0.35f, 0.45f, 0.60f, 0.50f);
            glBegin(GL_LINE_LOOP);
                glVertex3f(project(pX0, roofY + 0.5f, pZ0).x, project(pX0, roofY + 0.5f, pZ0).y, project(pX0, roofY + 0.5f, pZ0).z);
                glVertex3f(project(pX1, roofY + 0.5f, pZ0).x, project(pX1, roofY + 0.5f, pZ0).y, project(pX1, roofY + 0.5f, pZ0).z);
                glVertex3f(project(pX1, roofY + 0.5f, pZ1).x, project(pX1, roofY + 0.5f, pZ1).y, project(pX1, roofY + 0.5f, pZ1).z);
                glVertex3f(project(pX0, roofY + 0.5f, pZ1).x, project(pX0, roofY + 0.5f, pZ1).y, project(pX0, roofY + 0.5f, pZ1).z);
            glEnd();
        }
    }
    glLineWidth(1.0f);

    // Perimeter Railings (safety edge around roof)
    col(0.52f, 0.54f, 0.58f, 0.80f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
        glVertex3f(project(b.x + rInsetX, roofY + 3.0f, b.z + rInsetZ).x, project(b.x + rInsetX, roofY + 3.0f, b.z + rInsetZ).y, project(b.x + rInsetX, roofY + 3.0f, b.z + rInsetZ).z);
        glVertex3f(project(b.x + b.w - rInsetX, roofY + 3.0f, b.z + rInsetZ).x, project(b.x + b.w - rInsetX, roofY + 3.0f, b.z + rInsetZ).y, project(b.x + b.w - rInsetX, roofY + 3.0f, b.z + rInsetZ).z);
        glVertex3f(project(b.x + b.w - rInsetX, roofY + 3.0f, b.z + b.d - rInsetZ).x, project(b.x + b.w - rInsetX, roofY + 3.0f, b.z + b.d - rInsetZ).y, project(b.x + b.w - rInsetX, roofY + 3.0f, b.z + b.d - rInsetZ).z);
        glVertex3f(project(b.x + rInsetX, roofY + 3.0f, b.z + b.d - rInsetZ).x, project(b.x + rInsetX, roofY + 3.0f, b.z + b.d - rInsetZ).y, project(b.x + rInsetX, roofY + 3.0f, b.z + b.d - rInsetZ).z);
    glEnd();
    glLineWidth(1.0f);

    // Ladder (west side, vertical rungs)
    col(0.54f, 0.56f, 0.60f);
    glLineWidth(2.2f);
    float ladderX = b.x + rInsetX + 3.0f;
    float ladderZ = b.z + rInsetZ + 2.0f;
    float ladderH = 24.0f;
    glBegin(GL_LINES);
        // Left rail
        glVertex3f(project(ladderX, roofY, ladderZ).x, project(ladderX, roofY, ladderZ).y, project(ladderX, roofY, ladderZ).z);
        glVertex3f(project(ladderX, roofY + ladderH, ladderZ + 4.0f).x, project(ladderX, roofY + ladderH, ladderZ + 4.0f).y, project(ladderX, roofY + ladderH, ladderZ + 4.0f).z);
        // Right rail
        glVertex3f(project(ladderX + 6.0f, roofY, ladderZ).x, project(ladderX + 6.0f, roofY, ladderZ).y, project(ladderX + 6.0f, roofY, ladderZ).z);
        glVertex3f(project(ladderX + 6.0f, roofY + ladderH, ladderZ + 4.0f).x, project(ladderX + 6.0f, roofY + ladderH, ladderZ + 4.0f).y, project(ladderX + 6.0f, roofY + ladderH, ladderZ + 4.0f).z);
        // Rungs
        for (int rung = 0; rung < 8; ++rung) {
            float rungY = roofY + (rung + 1) * (ladderH / 8.0f);
            float rungZ = ladderZ + (rung + 1) * (4.0f / 8.0f);
            glVertex3f(project(ladderX, rungY, rungZ).x, project(ladderX, rungY, rungZ).y, project(ladderX, rungY, rungZ).z);
            glVertex3f(project(ladderX + 6.0f, rungY, rungZ).x, project(ladderX + 6.0f, rungY, rungZ).y, project(ladderX + 6.0f, rungY, rungZ).z);
        }
    glEnd();
    glLineWidth(1.0f);

    // Antenna Mast (tall center-back)
    col(0.62f, 0.64f, 0.67f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
        V3 a0 = project(b.x + b.w - rInsetX - 8.0f, roofY, b.z + b.d - rInsetZ - 8.0f);
        V3 a1 = project(b.x + b.w - rInsetX - 8.0f, roofY + 40.0f, b.z + b.d - rInsetZ - 8.0f);
        glVertex3f(a0.x, a0.y, a0.z);
        glVertex3f(a1.x, a1.y, a1.z);
    glEnd();
    glLineWidth(1.0f);

    // Redlight Beacon (antenna tip)
    col(0.90f, 0.35f, 0.35f, 0.85f);
    glPointSize(3.2f);
    glBegin(GL_POINTS);
        V3 beacon = project(b.x + b.w - rInsetX - 8.0f, roofY + 42.0f, b.z + b.d - rInsetZ - 8.0f);
        glVertex3f(beacon.x, beacon.y, beacon.z);
    glEnd();
}

void drawCar(const Car& c) {
    float bodyY = c.y;
    float bodyTopY = c.y + 9.0f;
    float cabinY = c.y + 12.0f;
    float roofY = c.y + 18.0f;
    float frontZ = c.forward ? (c.z + c.len) : c.z;
    float rearZ = c.forward ? c.z : (c.z + c.len);
    float beamDir = c.forward ? 1.0f : -1.0f;
    float beamEndZ = frontZ + (45.0f * beamDir);

    col(0.10f, 0.10f, 0.12f);
    quad4(project(c.x, bodyY, c.z), project(c.x + c.wid, bodyY, c.z), project(c.x + c.wid, bodyY, c.z + c.len), project(c.x, bodyY, c.z + c.len));
    quad4(project(c.x, bodyY, c.z), project(c.x, bodyTopY, c.z), project(c.x + c.wid, bodyTopY, c.z), project(c.x + c.wid, bodyY, c.z));
    quad4(project(c.x, bodyY, c.z + c.len), project(c.x + c.wid, bodyY, c.z + c.len), project(c.x + c.wid, bodyTopY, c.z + c.len), project(c.x, bodyTopY, c.z + c.len));
    quad4(project(c.x, bodyY, c.z), project(c.x, bodyY, c.z + c.len), project(c.x, bodyTopY, c.z + c.len), project(c.x, bodyTopY, c.z));
    quad4(project(c.x + c.wid, bodyY, c.z), project(c.x + c.wid, bodyY, c.z + c.len), project(c.x + c.wid, bodyTopY, c.z + c.len), project(c.x + c.wid, bodyTopY, c.z));

    float cabinInset = c.wid * 0.18f;
    float cabinInsetZ = c.len * 0.20f;
    col(0.22f, 0.22f, 0.25f);
    quad4(project(c.x + cabinInset, bodyTopY, c.z + cabinInsetZ),
        project(c.x + c.wid - cabinInset, bodyTopY, c.z + cabinInsetZ),
        project(c.x + c.wid - cabinInset, cabinY, c.z + c.len - cabinInsetZ),
        project(c.x + cabinInset, cabinY, c.z + c.len - cabinInsetZ));

    col(0.32f, 0.32f, 0.36f);
    quad4(project(c.x + cabinInset + 2.0f, cabinY, c.z + cabinInsetZ + 4.0f),
        project(c.x + c.wid - cabinInset - 2.0f, cabinY, c.z + cabinInsetZ + 4.0f),
        project(c.x + c.wid - cabinInset - 2.0f, roofY, c.z + c.len * 0.5f),
        project(c.x + cabinInset + 2.0f, roofY, c.z + c.len * 0.5f));

    col(0.18f, 0.18f, 0.21f);
    quad4(project(c.x + 1.5f, bodyTopY - 1.0f, c.z + 5.0f),
        project(c.x + c.wid - 1.5f, bodyTopY - 1.0f, c.z + 5.0f),
        project(c.x + c.wid - cabinInset, cabinY, c.z + cabinInsetZ + 3.0f),
        project(c.x + cabinInset, cabinY, c.z + cabinInsetZ + 3.0f));
    quad4(project(c.x + 1.5f, bodyTopY - 1.0f, c.z + c.len - 5.0f),
        project(c.x + c.wid - 1.5f, bodyTopY - 1.0f, c.z + c.len - 5.0f),
        project(c.x + c.wid - cabinInset, cabinY, c.z + c.len - cabinInsetZ - 3.0f),
        project(c.x + cabinInset, cabinY, c.z + c.len - cabinInsetZ - 3.0f));

    float lx1 = c.x + 6.5f;
    float lx2 = c.x + c.wid - 6.5f;
        col(1.0f, 0.92f, 0.38f, 0.85f);
    glPointSize(3.0f);
    glBegin(GL_POINTS);
      V3 h1 = project(lx1, c.y + 4.0f, frontZ);
      V3 h2 = project(lx2, c.y + 4.0f, frontZ);
      glVertex3f(h1.x, h1.y, h1.z);
      glVertex3f(h2.x, h2.y, h2.z);
    glEnd();

        col(1.0f, 0.92f, 0.35f, 0.08f);
    quad4(project(lx1 - 2.0f, c.y + 3.0f, frontZ),
        project(lx2 + 2.0f, c.y + 3.0f, frontZ),
        project(lx2 + 8.0f, c.y + 6.0f, beamEndZ),
        project(lx1 - 8.0f, c.y + 6.0f, beamEndZ));

        float trailEndZ = rearZ - (beamDir * 28.0f);
        col(1.0f, 0.08f, 0.08f, 0.24f);
    glBegin(GL_LINE_STRIP);
      V3 t1a = project(c.x + 5.0f, c.y + 4.0f, rearZ);
            V3 t1b = project(c.x + 5.0f, c.y + 4.0f, rearZ - (beamDir * 10.0f));
      V3 t1c = project(c.x + 5.0f, c.y + 4.0f, trailEndZ);
      glVertex3f(t1a.x, t1a.y, t1a.z);
      glVertex3f(t1b.x, t1b.y, t1b.z);
      glVertex3f(t1c.x, t1c.y, t1c.z);
    glEnd();
    glBegin(GL_LINE_STRIP);
      V3 t2a = project(c.x + c.wid - 5.0f, c.y + 4.0f, rearZ);
            V3 t2b = project(c.x + c.wid - 5.0f, c.y + 4.0f, rearZ - (beamDir * 10.0f));
      V3 t2c = project(c.x + c.wid - 5.0f, c.y + 4.0f, trailEndZ);
      glVertex3f(t2a.x, t2a.y, t2a.z);
      glVertex3f(t2b.x, t2b.y, t2b.z);
      glVertex3f(t2c.x, t2c.y, t2c.z);
    glEnd();
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glBegin(GL_QUADS);
        col(0.02f, 0.0f, 0.05f); glVertex3f(0, SH, -99);
        col(0.02f, 0.0f, 0.05f); glVertex3f(SW, SH, -99);
        col(0.05f, 0.02f, 0.1f); glVertex3f(SW, 0, -99);
        col(0.05f, 0.02f, 0.1f); glVertex3f(0, 0, -99);
    glEnd();

    col(0.06f, 0.06f, 0.07f);
    V3 g1 = project(-2000, 0, -2000);
    V3 g2 = project(2000, 0, -2000);
    V3 g3 = project(2000, 0, 2000);
    V3 g4 = project(-2000, 0, 2000);
    quad4(g1, g2, g3, g4);

    // Extensive ground detailing: patches, grids, grates, curbs, and markers.
    
    // Subtle worn texture patches (irregular placement for variety)
    col(0.09f, 0.09f, 0.10f, 0.32f);
    quad4(project(260, 0.8f, -1220), project(1080, 0.8f, -1220), project(1130, 0.8f, -890), project(300, 0.8f, -890));
    quad4(project(860, 0.8f, -220), project(1760, 0.8f, -220), project(1820, 0.8f, 200), project(900, 0.8f, 200));
    quad4(project(350, 0.8f, 380), project(1260, 0.8f, 380), project(1320, 0.8f, 740), project(390, 0.8f, 740));

    // Additional darker patches for texture variation
    col(0.07f, 0.07f, 0.08f, 0.25f);
    quad4(project(480, 0.8f, -550), project(720, 0.8f, -550), project(750, 0.8f, -280), project(500, 0.8f, -280));
    quad4(project(1100, 0.8f, 50), project(1380, 0.8f, 50), project(1420, 0.8f, 340), project(1140, 0.8f, 340));
    quad4(project(600, 0.8f, 520), project(880, 0.8f, 520), project(910, 0.8f, 820), project(630, 0.8f, 820));

    // Lighter weathered spots (reflective areas)
    col(0.12f, 0.12f, 0.13f, 0.18f);
    quad4(project(340, 0.8f, -950), project(520, 0.8f, -950), project(540, 0.8f, -750), project(360, 0.8f, -750));
    quad4(project(1200, 0.8f, -100), project(1360, 0.8f, -100), project(1380, 0.8f, 120), project(1220, 0.8f, 120));
    quad4(project(700, 0.8f, 650), project(920, 0.8f, 650), project(940, 0.8f, 900), project(720, 0.8f, 900));

    // Vertical grid lines (major lanes)
    col(0.16f, 0.16f, 0.17f, 0.18f);
    for (float x = 220.0f; x <= 1820.0f; x += 180.0f) {
        drawPathway(x, -1320.0f, x + 30.0f, 980.0f, 0.75f, 2.4f, 0.16f, 0.16f, 0.17f, 0.18f);
    }

    // Horizontal grid lines (cross-streets)
    col(0.13f, 0.13f, 0.14f, 0.22f);
    for (float z = -1240.0f; z <= 900.0f; z += 210.0f) {
        drawPathway(240.0f, z, 1840.0f, z + 35.0f, 0.75f, 2.0f, 0.13f, 0.13f, 0.14f, 0.22f);
    }

    // Additional finer grid for micro-detail (denser grid at lower opacity)
    col(0.11f, 0.11f, 0.12f, 0.10f);
    for (float x = 300.0f; x <= 1760.0f; x += 90.0f) {
        drawPathway(x, -1300.0f, x + 12.0f, 950.0f, 0.3f, 0.8f, 0.11f, 0.11f, 0.12f, 0.10f);
    }
    for (float z = -1180.0f; z <= 850.0f; z += 105.0f) {
        drawPathway(250.0f, z, 1850.0f, z + 18.0f, 0.3f, 0.75f, 0.11f, 0.11f, 0.12f, 0.10f);
    }

    // Metal grate textures (scattered across ground)
    col(0.14f, 0.15f, 0.17f, 0.28f);
    float grateSize = 60.0f;
    // Grate 1
    quad4(project(520, 0.9f, -400), project(520 + grateSize, 0.9f, -400), project(520 + grateSize, 0.9f, -400 + grateSize), project(520, 0.9f, -400 + grateSize));
    glLineWidth(1.2f);
    glBegin(GL_LINES);
    for (int i = 0; i <= 4; ++i) {
        float offset = i * (grateSize / 4.0f);
        glVertex3f(project(520 + offset, 0.95f, -400).x, project(520 + offset, 0.95f, -400).y, project(520 + offset, 0.95f, -400).z);
        glVertex3f(project(520 + offset, 0.95f, -400 + grateSize).x, project(520 + offset, 0.95f, -400 + grateSize).y, project(520 + offset, 0.95f, -400 + grateSize).z);
        glVertex3f(project(520, 0.95f, -400 + offset).x, project(520, 0.95f, -400 + offset).y, project(520, 0.95f, -400 + offset).z);
        glVertex3f(project(520 + grateSize, 0.95f, -400 + offset).x, project(520 + grateSize, 0.95f, -400 + offset).y, project(520 + grateSize, 0.95f, -400 + offset).z);
    }
    glEnd();

    // Grate 2
    col(0.14f, 0.15f, 0.17f, 0.28f);
    quad4(project(1300, 0.9f, 450), project(1300 + grateSize, 0.9f, 450), project(1300 + grateSize, 0.9f, 450 + grateSize), project(1300, 0.9f, 450 + grateSize));
    glLineWidth(1.2f);
    glBegin(GL_LINES);
    for (int i = 0; i <= 4; ++i) {
        float offset = i * (grateSize / 4.0f);
        glVertex3f(project(1300 + offset, 0.95f, 450).x, project(1300 + offset, 0.95f, 450).y, project(1300 + offset, 0.95f, 450).z);
        glVertex3f(project(1300 + offset, 0.95f, 450 + grateSize).x, project(1300 + offset, 0.95f, 450 + grateSize).y, project(1300 + offset, 0.95f, 450 + grateSize).z);
        glVertex3f(project(1300, 0.95f, 450 + offset).x, project(1300, 0.95f, 450 + offset).y, project(1300, 0.95f, 450 + offset).z);
        glVertex3f(project(1300 + grateSize, 0.95f, 450 + offset).x, project(1300 + grateSize, 0.95f, 450 + offset).y, project(1300 + grateSize, 0.95f, 450 + offset).z);
    }
    glEnd();
    glLineWidth(1.0f);

    col(0.05f, 0.05f, 0.07f);
    quad4(project(-150, 1, -2000), project(150, 1, -2000), project(150, 1, 2000), project(-150, 1, 2000));

    // Painted street markers (dashed center line)
    col(0.95f, 0.95f, 0.95f);
    for (float z = -1900; z < 1900; z += 170.0f) {
        quad4(project(-4, 1.3f, z), project(4, 1.3f, z), project(4, 1.3f, z + 80.0f), project(-4, 1.3f, z + 80.0f));
    }

    // Road condition variations (asphalt wear patterns)
    col(0.06f, 0.06f, 0.08f);
    quad4(project(-620, 1.05f, 300), project(-150, 1.05f, 300), project(-150, 1.05f, 470), project(-700, 1.05f, 560));

    col(0.13f, 0.13f, 0.16f);
    quad4(project(-190, 1.2f, -2000), project(-150, 1.2f, -2000), project(-150, 1.2f, 2000), project(-190, 1.2f, 2000));
    col(0.16f, 0.16f, 0.19f);
    quad4(project(150, 1.2f, -2000), project(190, 1.2f, -2000), project(190, 1.2f, 2000), project(150, 1.2f, 2000));

    drawStreetlight(-170, -1300, true);
    drawStreetlight(-170, -900, true);
    drawStreetlight(-170, -500, true);
    drawStreetlight(-170, -100, true);
    drawStreetlight(-170, 300, true);
    drawStreetlight(-170, 700, true);
    drawStreetlight(-170, 1100, true);
    drawStreetlight(170, -1300, false);
    drawStreetlight(170, -900, false);
    drawStreetlight(170, -500, false);
    drawStreetlight(170, -100, false);
    drawStreetlight(170, 300, false);
    drawStreetlight(170, 700, false);
    drawStreetlight(170, 1100, false);

    drawDumpster(-225, -360);
    drawDumpster(-225, -320);
    drawDumpster(-225, -280);
    drawGasStation();
    drawTopBenchArea();

    drawPathway(610, -1080, 770, -960, 75.0f, 12.0f, 0.55f, 0.95f, 1.0f, 0.12f);
    drawPathway(950, -900, 1105, -805, 80.0f, 11.0f, 0.85f, 0.65f, 1.0f, 0.10f);
    drawPathway(1460, -900, 1585, -790, 76.0f, 10.0f, 0.55f, 1.0f, 0.95f, 0.09f);
    drawPathway(1200, -650, 1335, -540, 68.0f, 10.0f, 0.60f, 0.85f, 1.0f, 0.08f);

    for (const auto& b : buildings) drawBuilding(b);

    drawPathway(430, -990, 620, -860, 2.5f, 12.0f, 0.65f, 0.95f, 1.0f, 0.08f);
    drawPathway(740, -860, 950, -710, 2.5f, 10.0f, 0.60f, 0.90f, 1.0f, 0.07f);
    drawPathway(1030, -730, 1230, -590, 2.5f, 10.0f, 0.55f, 0.85f, 1.0f, 0.065f);

    drawConvenienceStore();
    for (const auto& c : cars) drawCar(c);
    for (const auto& p : pedestrians) drawPedestrian(p);

    glDisable(GL_DEPTH_TEST);

    char liveText[160];
    std::snprintf(liveText, sizeof(liveText), "Live  screen:(%.1f, %.1f)  world:(%.1f, %.1f)", cursorSX, cursorSY, cursorWX, cursorWZ);
    drawText2D(18.0f, SH - 22.0f, liveText, 0.78f, 0.98f, 1.0f, 0.95f);
    drawText2D(18.0f, SH - 40.0f, "Left click to pin point and print coords in terminal", 0.65f, 0.80f, 0.90f, 0.9f);

    if (hasPinnedPoint) {
        char pinText[160];
        std::snprintf(pinText, sizeof(pinText), "Pinned screen:(%.1f, %.1f)  world:(%.1f, %.1f)", pinnedSX, pinnedSY, pinnedWX, pinnedWZ);
        drawText2D(18.0f, SH - 58.0f, pinText, 1.0f, 0.95f, 0.65f, 0.95f);

        col(1.0f, 0.95f, 0.65f, 0.8f);
        glLineWidth(1.5f);
        glBegin(GL_LINES);
            glVertex3f(pinnedSX - 8.0f, pinnedSY, 0.0f);
            glVertex3f(pinnedSX + 8.0f, pinnedSY, 0.0f);
            glVertex3f(pinnedSX, pinnedSY - 8.0f, 0.0f);
            glVertex3f(pinnedSX, pinnedSY + 8.0f, 0.0f);
        glEnd();
        glLineWidth(1.0f);
    }

    col(0.55f, 0.95f, 1.0f, 0.85f);
    glBegin(GL_LINES);
        glVertex3f(cursorSX - 6.0f, cursorSY, 0.0f);
        glVertex3f(cursorSX + 6.0f, cursorSY, 0.0f);
        glVertex3f(cursorSX, cursorSY - 6.0f, 0.0f);
        glVertex3f(cursorSX, cursorSY + 6.0f, 0.0f);
    glEnd();

    glEnable(GL_DEPTH_TEST);

    glutSwapBuffers();
}

void timer(int) {
    for (auto& c : cars) {
        c.z += c.forward ? c.speed : -c.speed;
        if (c.z > 1500) c.z = -1500;
        if (c.z < -1500) c.z = 1500;
    }
    
    // Update pedestrians
    for (auto& p : pedestrians) {
        updatePedestrian(p, 1.0f);
    }
    
    // Remove pedestrians that went out of bounds (z-axis only, x is unlimited for road-walking)
    pedestrians.erase(
        std::remove_if(pedestrians.begin(), pedestrians.end(),
            [](const Pedestrian& p) {
                return p.z < -1350.0f || p.z > 1000.0f;
            }),
        pedestrians.end()
    );
    
    // Spawn new pedestrians (won't exceed 100, higher spawn rate)
    if (pedestrians.size() < 100 && rand() % 100 < 30) {  // ~30% chance per frame
        spawnPedestrian();
    }
    
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

int main(int argc, char** argv) {
    srand(time(NULL));
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(SW, SH);
    glutCreateWindow("Isometric Cyberpunk");

    glOrtho(0, SW, 0, SH, -200, 200);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    updateCursorFromScreen(SW * 0.5f, SH * 0.5f);

    spawnBuilding(200, -800, 150, 400, 130, 1, true);
    spawnBuilding(300, -600, 180, 500, 130, 2, false);
    spawnBuilding(400, -400, 120, 350, 130, 3, true);
    spawnBuilding(250, -200, 160, 450, 130, 1, false);
    spawnBuilding(350, 0, 140, 380, 115, 2, true);
    spawnBuilding(320, 200, 170, 520, 130, 3, false);
    spawnBuilding(280, 400, 130, 420, 120, 1, true);
    spawnBuilding(380, 600, 175, 480, 135, 2, false);
    spawnBuilding(270, 800, 145, 360, 125, 3, true);
    spawnBuilding(340, 950, 155, 440, 140, 1, false);

    spawnBuilding(500, -1200, 150, 400, 120, 2, true);
    spawnBuilding(650, -1200, 160, 420, 125, 1, false);
    spawnBuilding(800, -1200, 140, 380, 130, 3, true);
    spawnBuilding(480, -1000, 170, 450, 120, 1, false);
    spawnBuilding(630, -1000, 155, 410, 125, 3, true);
    spawnBuilding(780, -1000, 165, 440, 130, 2, false);
    spawnBuilding(520, -800, 145, 390, 120, 3, true);
    spawnBuilding(670, -800, 180, 500, 125, 2, false);
    spawnBuilding(820, -800, 150, 420, 130, 1, true);
    spawnBuilding(500, -600, 160, 430, 120, 1, false);
    spawnBuilding(650, -600, 140, 380, 125, 2, true);
    spawnBuilding(800, -600, 170, 460, 130, 3, false);

    // Top-right / north-east cluster adjusted for visible projected range.
    spawnBuilding(910, -240, 135, 360, 110, 1, true);
    spawnBuilding(1030, -180, 130, 350, 105, 2, false);
    spawnBuilding(1140, -120, 125, 340, 100, 3, true);
    spawnBuilding(940, -20, 140, 370, 110, 1, false);
    spawnBuilding(1060, 40, 130, 350, 100, 2, true);
    spawnBuilding(1180, 110, 125, 330, 100, 3, false);
    spawnBuilding(980, 170, 135, 360, 105, 1, true);
    spawnBuilding(1110, 230, 120, 320, 95, 2, false);
    spawnBuilding(1260, -220, 130, 360, 100, 3, true);
    spawnBuilding(1380, -150, 125, 340, 95, 1, false);
    spawnBuilding(1510, -80, 120, 330, 95, 2, true);
    spawnBuilding(1640, -20, 115, 320, 90, 3, false);
    spawnBuilding(1270, 40, 125, 340, 95, 1, true);
    spawnBuilding(1400, 110, 120, 320, 90, 2, false);
    spawnBuilding(1530, 170, 115, 310, 90, 3, true);
    spawnBuilding(1660, 180, 110, 300, 85, 1, false);

    // Targeted fills around user-provided pin areas.
    spawnBuildingAtScreen(555, 977, 102, 300, 90, 1, true);
    spawnBuildingAtScreen(635, 940, 98, 285, 86, 2, false);
    spawnBuildingAtScreen(1489, 934, 108, 310, 92, 3, true);
    spawnBuildingAtScreen(1570, 900, 100, 290, 86, 1, false);

    // New requested additions.
    spawnBuildingAtScreen(1302, 1036, 96, 285, 86, 2, true);
    spawnBuildingAtScreen(305, 1015, 94, 275, 84, 1, false);
    spawnBuildingAtScreen(1345, 990, 98, 292, 88, 3, true);
    spawnBuildingAtScreen(1606, 795, 102, 300, 90, 2, false);

    // Buildings at PIN screen=(1392.0, 704.0) world=(629.4, 130.6)
    spawnBuildingAtScreen(1392.0f, 704.0f, 105, 310, 95, 1, true);
    spawnBuildingAtScreen(1392.0f, 704.0f, 98, 290, 88, 3, false);

    // Buildings at PIN screen=(1663.0, 623.0) world=(704.9, -106.9)
    spawnBuildingAtScreen(1663.0f, 623.0f, 102, 305, 92, 2, true);
    spawnBuildingAtScreen(1663.0f, 623.0f, 100, 295, 87, 1, false);

    for (int i = 0; i < 4; ++i) spawnCar(i);

    glutDisplayFunc(display);
    glutMouseFunc(onMouseClick);
    glutPassiveMotionFunc(onMouseMove);
    glutMotionFunc(onMouseMove);
    glutTimerFunc(0, timer, 0);
    glutMainLoop();
    return 0;
}
