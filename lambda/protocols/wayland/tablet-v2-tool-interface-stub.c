/* Link-time stub for cursor-shape-v1-protocol.c when tablet v2 is not implemented.
 * wp_cursor_shape_manager_v1 is bound at version 1, so get_tablet_tool_v2 is never used. */

#include "wayland-util.h"

static const struct wl_interface *tablet_tool_stub_types[] = {
    NULL,
    NULL,
    NULL,
    NULL,
};

static const struct wl_message zwp_tablet_tool_v2_stub_requests[] = {
    {"set_cursor", "u?oii", tablet_tool_stub_types + 0},
    {"destroy", "", tablet_tool_stub_types + 0},
};

static const struct wl_message zwp_tablet_tool_v2_stub_events[] = {
    {"type", "u", tablet_tool_stub_types + 0},
    {"hardware_serial", "uu", tablet_tool_stub_types + 0},
    {"hardware_id_wacom", "uu", tablet_tool_stub_types + 0},
    {"capability", "u", tablet_tool_stub_types + 0},
    {"done", "", tablet_tool_stub_types + 0},
    {"removed", "", tablet_tool_stub_types + 0},
    {"proximity_in", "uoo", tablet_tool_stub_types + 0},
    {"proximity_out", "", tablet_tool_stub_types + 0},
    {"down", "u", tablet_tool_stub_types + 0},
    {"up", "", tablet_tool_stub_types + 0},
    {"motion", "ff", tablet_tool_stub_types + 0},
    {"pressure", "u", tablet_tool_stub_types + 0},
    {"distance", "u", tablet_tool_stub_types + 0},
    {"tilt", "ff", tablet_tool_stub_types + 0},
    {"rotation", "f", tablet_tool_stub_types + 0},
    {"slider", "i", tablet_tool_stub_types + 0},
    {"wheel", "fi", tablet_tool_stub_types + 0},
    {"button", "uuu", tablet_tool_stub_types + 0},
    {"frame", "u", tablet_tool_stub_types + 0},
};

const struct wl_interface zwp_tablet_tool_v2_interface = {
    "zwp_tablet_tool_v2",
    2,
    2,
    zwp_tablet_tool_v2_stub_requests,
    19,
    zwp_tablet_tool_v2_stub_events,
};
