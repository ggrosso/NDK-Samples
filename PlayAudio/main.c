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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/neutrino.h>
#include <sys/stat.h>

#include <bps/bps.h>
#include <bps/audiomixer.h>
#include <bps/dialog.h>
#include <bps/mmrenderer.h>
#include <bps/navigator.h>
#include <mm/renderer.h>
#include <mm/renderer/events.h>

#include <screen/screen.h>

#include "dialogaudio.h"

/*
 * buffer to store messages that we will display in the dialog
 */
#define MSG_SIZE 1024
static char msg[MSG_SIZE];
static struct sigevent mmr_sigevent;

#if ! defined(DEFAULT_AUDIO_OUT)
    #define DEFAULT_AUDIO_OUT "audio:default"
#endif

#define COLOR_PURPLE 0xffff00ff
#define WAV_RELATIVE_PATH "/app/native/farewell.mp3"

static const char *errlist[] = {
    #define ERR( code ) [ code ] = #code
    ERR( MMR_ERROR_NONE ),
    ERR( MMR_ERROR_UNKNOWN ),
    ERR( MMR_ERROR_INVALID_PARAMETER ),
    ERR( MMR_ERROR_INVALID_STATE ),
    ERR( MMR_ERROR_UNSUPPORTED_VALUE ),
    ERR( MMR_ERROR_UNSUPPORTED_MEDIA_TYPE ),
    ERR( MMR_ERROR_MEDIA_PROTECTED ),
    ERR( MMR_ERROR_UNSUPPORTED_OPERATION ),
    ERR( MMR_ERROR_READ ),
    ERR( MMR_ERROR_WRITE ),
    ERR( MMR_ERROR_MEDIA_UNAVAILABLE ),
    ERR( MMR_ERROR_MEDIA_CORRUPTED ),
    ERR( MMR_ERROR_OUTPUT_UNAVAILABLE ),
    ERR( MMR_ERROR_NO_MEMORY ),
    ERR( MMR_ERROR_RESOURCE_UNAVAILABLE ),
    ERR( MMR_ERROR_MEDIA_DRM_NO_RIGHTS ),
    ERR( MMR_ERROR_DRM_CORRUPTED_DATA_STORE ),
    ERR( MMR_ERROR_DRM_OUTPUT_PROTECTION ),
    ERR( MMR_ERROR_DRM_OPL_HDMI ),
    ERR( MMR_ERROR_DRM_OPL_DISPLAYPORT ),
    ERR( MMR_ERROR_DRM_OPL_DVI ),
    ERR( MMR_ERROR_DRM_OPL_ANALOG_VIDEO ),
    ERR( MMR_ERROR_DRM_OPL_ANALOG_AUDIO ),
    ERR( MMR_ERROR_DRM_OPL_TOSLINK ),
    ERR( MMR_ERROR_DRM_OPL_SPDIF ),
    ERR( MMR_ERROR_DRM_OPL_BLUETOOTH ),
    ERR( MMR_ERROR_DRM_OPL_WIRELESSHD ),
    ERR( MMR_ERROR_MEDIA_DRM_EXPIRED_LICENSE )
    #undef ERR
};
#define NERRS ( sizeof(errlist) / sizeof(errlist[0]) )

static void mmrerror( mmr_context_t *ctxt, const char *errmsg ) {
    const mmr_error_info_t *err = mmr_error_info( ctxt );
    unsigned errcode = err->error_code;
    const char *name;
    if ( errcode >= NERRS || ( name = errlist[ errcode ] ) == NULL ) {
        name = "bad error code";
    }
    snprintf(msg, MSG_SIZE, "%s: error %d (%s)\n", errmsg, errcode, name );
    show_dialog_message(msg);
}

static int mmr_sigevent_handler(void *p) {
    int rc;
    mmr_context_t *ctxt = (mmr_context_t *)p;
    if (NULL == p) {
        show_dialog_message("mmr context error\n");
        return EXIT_FAILURE;
    }
    const mmr_event_t *mmr_event = mmr_event_get( ctxt );
    if (NULL == mmr_event) {
        show_dialog_message("mmr event error\n");
        return EXIT_FAILURE;
    }

    static mmr_state_t last_state = MMR_EVENT_NONE;
    static int last_speed = 0;

    if ((last_state != mmr_event->state) || (last_speed != mmr_event->speed)) {
        last_state = mmr_event->state;
        last_speed = mmr_event->speed;
        switch (mmr_event->state) {
            case MMR_STATE_PLAYING:
               if (0 == mmr_event->speed)
                   show_dialog_message("MMRenderer Status: Pausing\n");
               else 
                   show_dialog_message("MMRenderer Status: Playing\n");
               break;
            case MMR_STATE_STOPPED:
               show_dialog_message("MMRenderer Status: Stopped\n");
               break;
            case MMR_STATE_IDLE:
               show_dialog_message("MMRenderer Status: Idle\n");
               break;
            case MMR_STATE_DESTROYED:
               show_dialog_message("MMRenderer Status: Destroyed\n");
               break;
            default:
               show_dialog_message("MMRenderer Status: Unknown\n");
               break;
        }
    }

    if ((MMR_EVENT_ERROR == mmr_event->type) && (MMR_STATE_STOPPED == mmr_event->state)){
        /*
         * We have reached the end.  Do not rearm. Return failure to signal we 
         * don't want to be recalled.
         */
        show_dialog_message("MMRenderer Status: No more data\n");
        return EXIT_FAILURE;
    }

    rc = mmr_event_arm(ctxt, &mmr_sigevent);
    if ( rc > 0) {
        //event is already available, manually arm
        MsgDeliverEvent(0, &mmr_sigevent);
    } else if ( rc < 0 ) {
        show_dialog_message("mmr_event_arm() error\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static void handle_dialog_response_events(bps_event_t *event) {
    if (NULL == event) {
        return;
    }

    if (NULL == dialog_event_get_selected_context(event)) {
        return ;
    }

    float volume;
    int rc;

    if (0 == strcmp("query", dialog_event_get_selected_context(event))) {
        rc = audiomixer_get_output_level(AUDIOMIXER_OUTPUT_SPEAKER, &volume);
        if (BPS_SUCCESS == rc) {
            snprintf(msg, MSG_SIZE, "Successfully queried the output level.\ncurrent volume is %f\n", volume);
            show_dialog_message(msg);
        } else {
            show_dialog_message("Failed to query the output level.\n");
        }
    } else if (0 == strcmp("half", dialog_event_get_selected_context(event))) {
        rc = audiomixer_get_output_level(AUDIOMIXER_OUTPUT_SPEAKER, &volume);
        if (BPS_SUCCESS != rc) {
            show_dialog_message("Failed to query the output level.\n");
            return;
        }

        volume /= 2.0;

        rc = audiomixer_set_output_level(AUDIOMIXER_OUTPUT_SPEAKER, volume);
        if (BPS_SUCCESS == rc) {
            show_dialog_message("Successfully decreased the volume by half.\n");
        } else {
            show_dialog_message("Failed to decrease the volume.\n");
        }
    } else if (0 == strcmp("double", dialog_event_get_selected_context(event))) {
        rc = audiomixer_get_output_level(AUDIOMIXER_OUTPUT_SPEAKER, &volume);
        if (BPS_SUCCESS != rc) {
            show_dialog_message("Failed to query the output level.\n");
            return;
        }
        
        if (volume == 0.0) {
            volume = 2.0;
        } else {
            volume *= 2.0;
        }

        rc = audiomixer_set_output_level(AUDIOMIXER_OUTPUT_SPEAKER, volume);
        if (BPS_SUCCESS == rc) {
            show_dialog_message("Successfully doubled the volume.\n");
        } else {
            show_dialog_message("Failed to double the volume.\n");
        }
    } else if (0 == strcmp("toggle", dialog_event_get_selected_context(event))) {
        rc = audiomixer_toggle_output_mute(AUDIOMIXER_OUTPUT_SPEAKER);
        if (BPS_SUCCESS == rc) {
            show_dialog_message("Successfully toggled the mute setting.\n");
        } else {
            show_dialog_message("Failed to toggle the mute setting.\n");
        }
    }
}

static int drain_and_arm_mmr_events(mmr_context_t *ctxt) {
    const mmr_event_t *ev = mmr_event_get(ctxt);
    int rc;

    while (ev != NULL && ev->type != MMR_EVENT_NONE) {
        ev = mmr_event_get(ctxt);
    }
    if (NULL == ev) {
        return EXIT_FAILURE;
    }

    rc = mmr_event_arm(ctxt, &mmr_sigevent);
    if (rc < 0) {
        //errno has been set by mmr_event_arm; just return failure
        return EXIT_FAILURE;
    } else if (rc > 0) {
        //event is already available, manually arm
        MsgDeliverEvent(0, &mmr_sigevent);
    }
    //reach here if rc >= 0
    return EXIT_SUCCESS;
}

int main( int argc, char **argv ) {
    const char *mmrname = NULL;
    const char *ctxtname = "testplayer";
    const char *audioout = NULL;
    const char *inputtype = "track";
    char cwd[PATH_MAX];
    char inputurl[PATH_MAX];
    int rc;
    int final_return_code = EXIT_FAILURE;
    int exit_application = 0;
    mmr_connection_t *connection = NULL;
    mmr_context_t *ctxt = NULL;

    /*
     * Before we can listen for events from the BlackBerry(R) 10 OS platform
     * services, we need to initialize the BPS infrastructure
     */
    bps_initialize();

    if (setup_screen() != EXIT_SUCCESS) {
        printf("Unable to set up the screen. Exiting.");
        return 0;
    }

    /*
     * Once the BPS infrastructure has been initialized we can register for
     * events from the various BlackBerry(R) 10 OS platform services. The
     * Navigator service manages and delivers application life cycle and
     * visibility events.
     * For this sample, we request Navigator events so we can track when
     * the system is terminating the application (NAVIGATOR_EXIT event). 
     * This allows us to clean up application resources.
     */
    navigator_request_events(0);
    dialog_request_events(0);

    /*
     * Create and display the dialog.
     */
    create_dialog();

    getcwd(cwd, PATH_MAX);
    rc = snprintf(inputurl, PATH_MAX, "file://%s%s", cwd, WAV_RELATIVE_PATH);
    if (rc > PATH_MAX - 1)
    {
          show_dialog_message("File name and path too long");
          goto fail;
    }

    mode_t mode = S_IRUSR | S_IXUSR;
    int audio_oid; // output ID
    strm_dict_t *aoparams = NULL; // output parameters

    audioout = DEFAULT_AUDIO_OUT;

    if ( ( connection = mmr_connect( mmrname ) ) == NULL ) {
        snprintf(msg, MSG_SIZE, "mmr_connect: %s", strerror(errno));
        show_dialog_message(msg);
    } else if ( ( ctxt = mmr_context_create( connection, ctxtname, 0, mode ) ) == NULL ) {
        snprintf(msg, MSG_SIZE, "%s: %s", ctxtname, strerror(errno));
        show_dialog_message(msg);
    } else if ( audioout && ( audio_oid = mmr_output_attach( ctxt, audioout, "audio" ) ) < 0 ) {
        mmrerror( ctxt, audioout );
    } else if ( aoparams && mmr_output_parameters( ctxt, audio_oid, aoparams ) ) {
        mmrerror( ctxt, "output parameters (audio)" );
    } else if ( mmr_input_attach( ctxt, inputurl, inputtype ) < 0 ) {
        mmrerror( ctxt, inputurl );
    } else if ( mmr_play( ctxt ) < 0 ) {
        mmrerror( ctxt, "mmr_play" );
    } else if (BPS_SUCCESS != bps_add_sigevent_handler( &mmr_sigevent, mmr_sigevent_handler, ctxt ) ) { 
        snprintf( msg, MSG_SIZE, "bps_add_sigevent_handler() failure %s", strerror( errno ) );
        show_dialog_message( msg );
    } else if ( drain_and_arm_mmr_events ( ctxt ) ) {
        snprintf( msg, MSG_SIZE, "drain_and_arm_mmr_events() failure %s", strerror( errno ) );
        show_dialog_message( msg );
    } else {
        show_dialog_message( "Playing Audio\n" );
        final_return_code = EXIT_SUCCESS;
    }

fail:
    /*
     * Process Navigator events until we receive a NAVIGATOR_EXIT event.
     */
    while (!exit_application) {
        /*
         * Using a negative timeout (-1) in the call to bps_get_event(...)
         * ensures that we don't busy wait by blocking until an event is
         * available.
         */
        bps_event_t *event = NULL;
        bps_get_event(&event, -1);

        if (event) {
            /*
             * If it is a NAVIGATOR_EXIT event then set the exit_application
             * flag so the application will stop processing events, clean up
             * and exit
             */
            if (bps_event_get_domain(event) == navigator_get_domain()) {
                if (NAVIGATOR_EXIT == bps_event_get_code(event)) {
                    if (final_return_code == EXIT_SUCCESS) {
                        mmr_stop( ctxt );             // Not really necessary -- mmr_input_detach() would take care of this
                        mmr_input_detach( ctxt );     // Not really necessary -- mmr_context_destroy()  would take care of this
                        mmr_context_destroy( ctxt );  // Not really necessary -- mmr_disconnect() would take care of this
                        mmr_disconnect( connection ); // Not really necessary -- exiting would take care of this
                    }
                    exit_application = 1;
                }
            } else if (bps_event_get_domain(event) == dialog_get_domain()) {
                if (DIALOG_RESPONSE == bps_event_get_code(event)) {
                    handle_dialog_response_events(event);
                }
            }
        }
    }

    /*
     * Destroy the dialog, if it exists and cleanup screen resources.
     */
    destroy_dialog();
    cleanup_screen();
    /*
     * Clean up the BPS infrastructure and exit
     */
    bps_shutdown();

    return final_return_code;

}
