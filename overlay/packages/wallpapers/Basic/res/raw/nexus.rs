// Copyright (C) 2009 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma version(1)
#pragma stateVertex(PVOrtho)
#pragma stateStore(PSSolid)

#define MAX_PULSES           20
#define MAX_EXTRAS           40
#define PULSE_SIZE           14 // Size in pixels of a cell
#define HALF_PULSE_SIZE      7
#define GLOW_SIZE            64 // Size of the leading glow in pixels
#define HALF_GLOW_SIZE       32
#define SPEED                0.2f // (200 / 1000) Pixels per ms
#define SPEED_VARIANCE       0.3f
#define PULSE_NORMAL         0
#define PULSE_EXTRA          1
#define TRAIL_SIZE           40 // Number of cells in a trail
#define MAX_DELAY	         2000 // Delay between a pulse going offscreen and restarting

struct pulse_s {
    int pulseType;
    float originX;
    float originY;
    int color;
    int startTime;
    float dx;
    float dy;
    float scale;
    int active;
};
struct pulse_s gPulses[MAX_PULSES];

struct pulse_s gExtras[MAX_EXTRAS];

int gNow;


void setColor(int c) {
    if (State->mode == 1) {
        // sholes red
        color(0.9f, 0.1f, 0.1f, 0.8f);
    } else if (c == 0) {
        // red
        color(1.0f, 0.0f, 0.0f, 0.8f);
    } else if (c == 1) {
        // green
        color(0.0f, 0.8f, 0.0f, 0.8f);
    } else if (c == 2) {
        // blue
        color(0.0f, 0.4f, 0.9f, 0.8f);
    } else if (c == 3) {
        // yellow
        color(1.0f, 0.8f, 0.0f, 0.8f);
    }
}

void initPulse(struct pulse_s * pulse, int pulseType) {
    float scale = randf2(0.7f, 1.7f);
    pulse->scale = scale;
    if (randf(1) > 0.5f) {
        pulse->originX = (int)randf(State->width * 2 / PULSE_SIZE) * PULSE_SIZE;
        pulse->dx = 0;
        if (randf(1) > 0.5f) {
            // Top
            pulse->originY = 0;
            pulse->dy = scale;
        } else {
            // Bottom
            pulse->originY = State->height / scale;
            pulse->dy = -scale;
        }
    } else {
        pulse->originY = (int)randf(State->height / PULSE_SIZE) * PULSE_SIZE;
        pulse->dy = 0;
        if (randf(1) > 0.5f) {
            // Left
            pulse->originX = 0;
            pulse->dx = scale;
        } else {
            // Right
            pulse->originX = State->width * 2 / scale;
            pulse->dx = -scale;
        }
    }
    pulse->startTime = gNow + (int)randf(MAX_DELAY);

    pulse->color = (int)randf(4.0f);

    pulse->pulseType = pulseType;
    if (pulseType == PULSE_EXTRA) {
        pulse->active = 0;
    } else {
        pulse->active = 1;
    }
}

void initPulses() {
    gNow = uptimeMillis();
    int i;
    for (i=0; i<MAX_PULSES; i++) {
        initPulse(&gPulses[i], PULSE_NORMAL);
    }
    for (i=0; i<MAX_EXTRAS; i++) {
        struct pulse_s * p = &gExtras[i];
        p->pulseType = PULSE_EXTRA;
        p->active = 0;
    }
}

void drawBackground(int width, int height) {
	bindProgramFragment(NAMED_PFTextureBG);
    bindTexture(NAMED_PFTextureBG, 0, NAMED_TBackground);
    color(1.0f, 1.0f, 1.0f, 1.0f);
    if (State->rotate) {
        drawRect(0.0f, 0.0f, height*2, width, 0.0f);
    } else {
    	drawRect(0.0f, 0.0f, width*2, height, 0.0f);
   	}
}

void drawPulses(struct pulse_s * pulseSet, int setSize) {
	bindProgramFragment(NAMED_PFTexture);
    bindProgramFragmentStore(NAMED_PSBlend);

    float matrix[16];
    float modelMatrix[16];

    int i;
    for (i=0; i<setSize; i++) {
    	struct pulse_s * p = &pulseSet[i];

 	    int delta = gNow - p->startTime;

    	if (p->active != 0 && delta >= 0) {

	        matrixLoadIdentity(modelMatrix);
            if (State->rotate) {
                //matrixLoadRotate(modelMatrix, 90.0f, 0.0f, 0.0f, 1.0f);
                //matrixTranslate(modelMatrix, 0.0f, -height, 1.0f);
                // XXX: HAX: do not slide display in landscape
            } else {
                 matrixTranslate(modelMatrix, -(State->xOffset * State->width), 0, 0);
            }
            matrixScale(modelMatrix, p->scale, p->scale, 1.0f);
            vpLoadModelMatrix(modelMatrix);

	        float x = p->originX + (p->dx * SPEED * delta);
	        float y = p->originY + (p->dy * SPEED * delta);

	        matrixLoadIdentity(matrix);
	        if (p->dx < 0) {
	            vpLoadTextureMatrix(matrix);
	            float xx = x + (TRAIL_SIZE * PULSE_SIZE);
	            if (xx <= 0) {
	                initPulse(p, p->pulseType);
	            } else {
	                setColor(p->color);
	                bindTexture(NAMED_PFTexture, 0, NAMED_TPulse);
	                drawRect(x, y, xx, y + PULSE_SIZE, 0.0f);
	                bindTexture(NAMED_PFTexture, 0, NAMED_TGlow);
	                drawRect(x + HALF_PULSE_SIZE - HALF_GLOW_SIZE,
	                    y + HALF_PULSE_SIZE - HALF_GLOW_SIZE,
	                    x + HALF_PULSE_SIZE + HALF_GLOW_SIZE,
	                    y + HALF_PULSE_SIZE + HALF_GLOW_SIZE,
	                    0.0f);
	            }
	        } else if (p->dx > 0) {
				x += PULSE_SIZE; // need to start on the other side of this cell
	            vpLoadTextureMatrix(matrix);
	            float xx = x - (TRAIL_SIZE * PULSE_SIZE);
	 	        if (xx >= State->width * 2) {
	               initPulse(p, p->pulseType);
	            } else {
	                setColor(p->color);
	                bindTexture(NAMED_PFTexture, 0, NAMED_TPulse);
	                drawRect(x, y, xx, y + PULSE_SIZE, 0.0f);
	                bindTexture(NAMED_PFTexture, 0, NAMED_TGlow);
	                drawRect(x - HALF_PULSE_SIZE - HALF_GLOW_SIZE,
	                    y + HALF_PULSE_SIZE - HALF_GLOW_SIZE,
	                    x - HALF_PULSE_SIZE + HALF_GLOW_SIZE,
	                    y + HALF_PULSE_SIZE + HALF_GLOW_SIZE,
	                    0.0f);
	            }
	        } else if (p->dy < 0) {
	            vpLoadTextureMatrix(matrix);
	            float yy = y + (TRAIL_SIZE * PULSE_SIZE);
	            if (yy <= 0) {
	               initPulse(p, p->pulseType);
	            } else {
	                setColor(p->color);
	                bindTexture(NAMED_PFTexture, 0, NAMED_TPulseVert);
	                drawRect(x, yy, x + PULSE_SIZE, y, 0.0f);
	                bindTexture(NAMED_PFTexture, 0, NAMED_TGlow);
	                drawRect(x + HALF_PULSE_SIZE - HALF_GLOW_SIZE,
	                    y + HALF_PULSE_SIZE - HALF_GLOW_SIZE,
	                    x + HALF_PULSE_SIZE + HALF_GLOW_SIZE,
	                    y + HALF_PULSE_SIZE + HALF_GLOW_SIZE,
	                    0.0f);
	            }
	        } else if (p->dy > 0) {
				y += PULSE_SIZE; // need to start on the other side of this cell
	            vpLoadTextureMatrix(matrix);
	            float yy = y - (TRAIL_SIZE * PULSE_SIZE);
	            if (yy >= State->height) {
	               initPulse(p, p->pulseType);
	            } else {
	                setColor(p->color);
	                bindTexture(NAMED_PFTexture, 0, NAMED_TPulseVert);
	                drawRect(x, yy, x + PULSE_SIZE, y, 0.0f);
	                bindTexture(NAMED_PFTexture, 0, NAMED_TGlow);
	                drawRect(x + HALF_PULSE_SIZE - HALF_GLOW_SIZE,
	                    y - HALF_PULSE_SIZE - HALF_GLOW_SIZE,
	                    x + HALF_PULSE_SIZE + HALF_GLOW_SIZE,
	                    y - HALF_PULSE_SIZE + HALF_GLOW_SIZE,
	                    0.0f);
	            }
	        }
	    }
    }


    matrixLoadIdentity(matrix);
    vpLoadTextureMatrix(matrix);
}

void addTap(int x, int y) {
    int i;
    int count = 0;
    int color = (int)randf(4.0f);
    float scale = randf2(0.9f, 1.9f);
    x = (int)(x / PULSE_SIZE) * PULSE_SIZE;
    y = (int)(y / PULSE_SIZE) * PULSE_SIZE;
    for (i=0; i<MAX_EXTRAS; i++) {
    	struct pulse_s * p = &gExtras[i];
    	if (p->active == 0) {
            p->originX = x/scale;
            p->originY = y/scale;
            p->scale = scale;

            if (count == 0) {
                p->dx = scale;
                p->dy = 0.0f;
            } else if (count == 1) {
                p->dx = -scale;
                p->dy = 0.0f;
            } else if (count == 2) {
                p->dx = 0.0f;
                p->dy = scale;
            } else if (count == 3) {
                p->dx = 0.0f;
                p->dy = -scale;
            }

            p->active = 1;
            p->color = color;
            color++;
            if (color >= 4) {
                color = 0;
            }
            p->startTime = gNow;
            count++;
            if (count == 4) {
                break;
            }
        }
    }
}

int main(int index) {

    gNow = uptimeMillis();

    if (Command->command != 0) {
        //debugF("x", Command->x);
        //debugF("y", Command->y);
        Command->command = 0;
        addTap(Command->x, Command->y);
    }

    int width = State->width;
    int height = State->height;

    float matrix[16];
    matrixLoadIdentity(matrix);
    if (State->rotate) {
        //matrixLoadRotate(matrix, 90.0f, 0.0f, 0.0f, 1.0f);
        //matrixTranslate(matrix, 0.0f, -height, 1.0f);
        // XXX: HAX: do not slide display in landscape
    } else {
         matrixTranslate(matrix, -(State->xOffset * width), 0, 0);
    }

    vpLoadModelMatrix(matrix);

    drawBackground(width, height);

    drawPulses(gPulses, MAX_PULSES);
    drawPulses(gExtras, MAX_EXTRAS);

    return 45;
}
