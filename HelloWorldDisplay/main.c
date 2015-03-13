/*
 * Copyright (c) 2011-2012 Research In Motion Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "bbutil.h"

#include <png.h>

#include <bps/navigator.h>
#include <bps/screen.h>
#include <bps/bps.h>
#include <bps/event.h>

#include <screen/screen.h>
#include <sys/keycodes.h>

#include <EGL/egl.h>
#include <GLES/gl.h>

#include <math.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float width, height;
static GLuint background;
static GLfloat vertices[8];
static GLfloat tex_coord[8];
static screen_context_t screen_ctx;
static float pos_x, pos_y;
const char *message = "Hello world";

static font_t* font;

int init() {
    EGLint surface_width, surface_height;

    //Load background texture
    float tex_x, tex_y;
    int size_x, size_y;

    if (EXIT_SUCCESS
            != bbutil_load_texture("app/native/HelloWorld_bubble_portrait.png",
                    &size_x, &size_y, &tex_x, &tex_y, &background)) {
        fprintf(stderr, "Unable to load background texture\n");
        return EXIT_FAILURE;
    }

    //Query width and height of the window surface created by utility code
    eglQuerySurface(egl_disp, egl_surf, EGL_WIDTH, &surface_width);
    eglQuerySurface(egl_disp, egl_surf, EGL_HEIGHT, &surface_height);

    EGLint err = eglGetError();
    if (err != 0x3000) {
        fprintf(stderr, "Unable to query EGL surface dimensions\n");
        return EXIT_FAILURE;
    }

    width = (float) surface_width;
    height = (float) surface_height;

    int dpi = bbutil_calculate_dpi(screen_ctx);

    //As bbutil renders text using device-specifc dpi, we need to compute a point size
    //for the font, so that the text string fits into the bubble. We use 15 pt as our
    //font size.
    //
    // This app assumes the use of a Z10 in portrait mode. For other devices and
    // orientations, you need to modify the code and settings accordingly.
    // font with point size of
    //15 fits into the bubble texture.
    const float Z10_DPI = 358.0f;
    const float FONT_PT_SIZE = 15.0f;
    float stretch_factor = (float)surface_width / (float)size_x;
    int point_size = (int)(FONT_PT_SIZE * stretch_factor / ((float)dpi / Z10_DPI ));

    font = bbutil_load_font("/usr/fonts/font_repository/monotype/arial.ttf", point_size, dpi);

    if (!font) {
        return EXIT_FAILURE;
    }

    //Initialize GL for 2D rendering
    glViewport(0, 0, (int) width, (int) height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    glOrthof(0.0f, width / height, 0.0f, 1.0f, -1.0f, 1.0f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    //Set world coordinates to coincide with screen pixels
    glScalef(1.0f / height, 1.0f / height, 1.0f);

    float text_width, text_height;
    bbutil_measure_text(font, message, &text_width, &text_height);
    pos_x = (width - text_width) / 2;
    pos_y = height / 2;

    //Setup background polygon
    vertices[0] = 0.0f;
    vertices[1] = 0.0f;
    vertices[2] = width;
    vertices[3] = 0.0f;
    vertices[4] = 0.0f;
    vertices[5] = height;
    vertices[6] = width;
    vertices[7] = height;

    tex_coord[0] = 0.0f;
    tex_coord[1] = 0.0f;
    tex_coord[2] = tex_x;
    tex_coord[3] = 0.0f;
    tex_coord[4] = 0.0f;
    tex_coord[5] = tex_y;
    tex_coord[6] = tex_x;
    tex_coord[7] = tex_y;

    return EXIT_SUCCESS;
}

void render() {
    //Typical rendering pass
    glClear(GL_COLOR_BUFFER_BIT);

    //Render background quad first
    glEnable(GL_TEXTURE_2D);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glTexCoordPointer(2, GL_FLOAT, 0, tex_coord);
    glBindTexture(GL_TEXTURE_2D, background);

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_TEXTURE_2D);

    //Use utility code to render welcome text onto the screen
    bbutil_render_text(font, message, pos_x, pos_y, 0.35f, 0.35f, 0.35f, 1.0f);

    //Use utility code to update the screen
    bbutil_swap();
}

int main(int argc, char **argv) {

    int rc = 0;

    //Create a screen context that will be used to create an EGL surface to to receive libscreen events
    rc = screen_create_context(&screen_ctx, SCREEN_APPLICATION_CONTEXT);
    if (BPS_SUCCESS != rc)
    {
        fprintf(stderr, "Failed to create context.\n");
        return rc;
    }

    //Initialize BPS library
    rc = bps_initialize();
    if (BPS_SUCCESS != rc)
    {
        fprintf(stderr, "Failed to initialize BPS.\n");
        return rc;
    }

    //Use utility code to initialize EGL for rendering with GL ES 1.1
    rc = bbutil_init_egl(screen_ctx);
    if (EXIT_SUCCESS != rc) {
        fprintf(stderr, "Unable to initialize EGL\n");
        screen_destroy_context(screen_ctx);
        return rc;
    }

    //Initialize app data
    rc = init();
    if (EXIT_SUCCESS != rc) {
        fprintf(stderr, "Unable to initialize app logic\n");
        bbutil_terminate();
        screen_destroy_context(screen_ctx);
        return rc;
    }

    //Signal BPS library that navigator and screen events will be requested
    rc = screen_request_events(screen_ctx);
    if (BPS_SUCCESS != rc) {
        fprintf(stderr, "screen_request_events failed\n");
        bbutil_terminate();
        screen_destroy_context(screen_ctx);
        return rc;
    }

    rc = navigator_request_events(0);
    if (BPS_SUCCESS != rc) {
        fprintf(stderr, "navigator_request_events failed\n");
        bbutil_terminate();
        screen_destroy_context(screen_ctx);
        return rc;
    }

    for (;;) {
        //Request and process BPS next available event
        bps_event_t *event = NULL;
        if (BPS_SUCCESS != bps_get_event(&event, 0)) {
            fprintf(stderr, "bps_get_event failed\n");
            break;
        }

        if ((event) && (bps_event_get_domain(event) == navigator_get_domain())
                && (NAVIGATOR_EXIT == bps_event_get_code(event))) {
            break;
        }

        render();
    }

    //Stop requesting events from libscreen
    screen_stop_events(screen_ctx);

    //Shut down BPS library for this process
    bps_shutdown();

    //Destroy the font
    bbutil_destroy_font(font);

    //Use utility code to terminate EGL setup
    bbutil_terminate();

    //Destroy libscreen context
    screen_destroy_context(screen_ctx);
    return rc;
}
