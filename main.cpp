#include <GL/glut.h>

float carX = 0.0;

void drawBuilding(int x, int y, int w, int h) {
    glColor3f(0.05, 0.05, 0.1); 
    glBegin(GL_QUADS);
        glVertex2i(x, y); glVertex2i(x+w, y); glVertex2i(x+w, y+h); glVertex2i(x, y+h);
    glEnd();
    glColor3f(0.0, 1.0, 1.0); 
    glBegin(GL_LINE_LOOP);
        glVertex2i(x, y); glVertex2i(x+w, y); glVertex2i(x+w, y+h); glVertex2i(x, y+h);
    glEnd();
}

void timer(int) {
    glutPostRedisplay(); // Redraw the screen
    glutTimerFunc(1000/60, timer, 0); // 60 FPS
    
    carX += 1.0; // Move car
    if (carX > 800) carX = -50; // Reset when off-screen
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT);

    // 1. Draw Floor Grid
    glColor3f(1.0, 0.0, 1.0);
    glBegin(GL_LINES);
    for(int i = 0; i <= 800; i += 40) {
        glVertex2i(i, 0); glVertex2i(400, 250);
        if (i <= 240) { glVertex2i(0, i); glVertex2i(800, i); }
    }
    glEnd();

    // 2. Draw Buildings
    drawBuilding(50, 200, 100, 300);
    drawBuilding(200, 220, 80, 250);
    drawBuilding(600, 200, 120, 350);

    // 3. Draw Moving Car (Simple Rectangle for now)
    glColor3f(1.0, 1.0, 0.0); // Yellow car
    glBegin(GL_QUADS);
        glVertex2f(carX, 100);
        glVertex2f(carX + 40, 100);
        glVertex2f(carX + 40, 120);
        glVertex2f(carX, 120);
    glEnd();

    glutSwapBuffers(); // Use this instead of glFlush for smooth animation
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    // Use GLUT_DOUBLE for smooth animation (no flickering)
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB); 
    glutInitWindowSize(800, 600);
    glutCreateWindow("Neon-Grid Metropolis");
    
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glMatrixMode(GL_PROJECTION);
    gluOrtho2D(0.0, 800.0, 0.0, 600.0);

    glutDisplayFunc(display);
    glutTimerFunc(0, timer, 0); // Start the timer
    glutMainLoop();
    return 0;
}