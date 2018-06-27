#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xcb_util.h>
#include <xcb/xproto.h>
#include <xcb/randr.h>
#include "xws.h"

static xcb_atom_t backlight, backlight_new, backlight_legacy;
static xcb_connection_t *conn;

static long xws_backlight_get(xcb_randr_output_t output) {
    xcb_generic_error_t *error;
    xcb_randr_get_output_property_reply_t *prop_reply = NULL;
    xcb_randr_get_output_property_cookie_t prop_cookie;
    long value;
    
    backlight = backlight_new;
    prop_cookie = xcb_randr_get_output_property(conn, output, backlight, XCB_ATOM_NONE, 0, 4, 0, 0);
    prop_reply = xcb_randr_get_output_property_reply(conn, prop_cookie, &error);

    if (error != NULL || prop_reply == NULL) {
        backlight = backlight_legacy;

        if (backlight != XCB_ATOM_NONE) {
            prop_cookie = xcb_randr_get_output_property(conn, output, backlight, XCB_ATOM_NONE, 0, 4, 0, 0);
            prop_reply = xcb_randr_get_output_property_reply(conn, prop_cookie, &error);
            if (error != NULL || prop_reply == NULL) {
                return -1;
            }
        }
    }

    if (prop_reply == NULL ||
        prop_reply->type != XCB_ATOM_INTEGER ||
        prop_reply->num_items != 1 ||
        prop_reply->format != 32) {
	    value = -1;
    } else {
	    value = *((int32_t *) xcb_randr_get_output_property_data(prop_reply));
    }

    free(prop_reply);
    return value;
}

int xws_backlight_set(long value) {
    xcb_screen_iterator_t iter;
    xcb_generic_error_t *error;
    int success = -1;

    iter = xcb_setup_roots_iterator(xcb_get_setup(conn));

    while (iter.rem) {
        xcb_screen_t *screen = iter.data;
        xcb_window_t root = screen->root;
        xcb_randr_output_t *outputs;

        xcb_randr_get_screen_resources_cookie_t resources_cookie;
        xcb_randr_get_screen_resources_reply_t *resources_reply;

        resources_cookie = xcb_randr_get_screen_resources(conn, root);
        resources_reply = xcb_randr_get_screen_resources_reply(conn, resources_cookie, &error);

        if (error != NULL || resources_reply == NULL) {
            int ec = error ? error->error_code : -1;
            fprintf(stderr, "RANDR Get Screen Resources returned error %d\n", ec);
            continue;
        }

        outputs = xcb_randr_get_screen_resources_outputs(resources_reply);

        for (int o = 0; o < resources_reply->num_outputs; o++) {
            xcb_randr_output_t output = outputs[o];
            double scaling_factor;
            double new;
            double min, max;

            if (xws_backlight_get(output) != -1) {
                xcb_randr_query_output_property_cookie_t prop_cookie;
                xcb_randr_query_output_property_reply_t *prop_reply;

                prop_cookie = xcb_randr_query_output_property(conn, output, backlight);
                prop_reply = xcb_randr_query_output_property_reply(conn, prop_cookie, &error);

                if (error != NULL || prop_reply == NULL) continue;

                if (prop_reply->range && xcb_randr_query_output_property_valid_values_length(prop_reply) == 2) {
                    int32_t* values = xcb_randr_query_output_property_valid_values(prop_reply);

                    min = values[0];
                    max = values[1];

                    scaling_factor = (max - min) / 100;
                    new = value * scaling_factor;

                    if (new > max) new = max;
                    if (new < min) new = min;

                    value = (long)new;

                    xcb_randr_change_output_property(conn, output, backlight, XCB_ATOM_INTEGER, 32,
                        XCB_PROP_MODE_REPLACE, 1, (unsigned char*)&value);
                    xcb_flush(conn);

                    success = 1;
                }

                free(prop_reply);
            }
        }

        free(resources_reply);
        xcb_screen_next(&iter);
    }

    xcb_aux_sync(conn);

    return success;
}

void xws_init(char* display_name) {
    int backlight_len = strlen("backlight");
    xcb_generic_error_t *error;

    xcb_randr_query_version_cookie_t ver_cookie;
    xcb_randr_query_version_reply_t* ver_reply;

    xcb_intern_atom_cookie_t backlight_cookie[2];
    xcb_intern_atom_reply_t *backlight_reply;

    conn = xcb_connect(display_name, NULL);
    ver_cookie = xcb_randr_query_version(conn, 1, 2);
    ver_reply = xcb_randr_query_version_reply(conn, ver_cookie, &error);

    if (error != NULL || ver_reply == NULL) {
		int ec = error ? error->error_code : -1;
		fprintf (stderr, "RANDR Query Version returned error %d\n", ec);
		exit(EXIT_FAILURE);
    }

    if (ver_reply->major_version != 1 || ver_reply->minor_version < 2) {
		fprintf (stderr, "RandR version %d.%d too old\n", ver_reply->major_version, ver_reply->minor_version);
		exit(EXIT_FAILURE);
    }

    free(ver_reply);

    backlight_cookie[0] = xcb_intern_atom(conn, 1, backlight_len, "Backlight");
    backlight_cookie[1] = xcb_intern_atom(conn, 1, backlight_len, "BACKLIGHT");

    backlight_reply = xcb_intern_atom_reply(conn, backlight_cookie[0], &error);
    if (error != NULL || backlight_reply == NULL) {
        int ec = error ? error->error_code : -1;
        fprintf(stderr, "Intern Atom returned error %d\n", ec);
        exit(EXIT_FAILURE);
    }

    backlight_new = backlight_reply->atom;
    free(backlight_reply);

    if (backlight_new == XCB_NONE) {
        backlight_reply = xcb_intern_atom_reply(conn, backlight_cookie[1], &error);

        if (error != NULL || backlight_reply == NULL) {
            int ec = error ? error->error_code : -1;
            fprintf(stderr, "Intern Atom returned error %d\n", ec);
            exit(EXIT_FAILURE);
        }

        backlight_legacy = backlight_reply->atom;
        free(backlight_reply);

        if (backlight_legacy == XCB_NONE) {
            fprintf(stderr, "No outputs have backlight property\n");
            exit(EXIT_FAILURE);
        }
    }
}
