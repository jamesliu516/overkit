// Copyright (c) 2020 Matthew J. Smith and Overkit contributors
// License: MIT (http://opensource.org/licenses/MIT)

#ifndef OVK_CORE_CONNECTIVITY_COMPONENT_H_INCLUDED
#define OVK_CORE_CONNECTIVITY_COMPONENT_H_INCLUDED

#include <ovk/core/Global.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  OVK_CONNECTIVITY_EVENT_FLAGS_NONE = 0,
  OVK_CONNECTIVITY_EVENT_FLAGS_CREATE = 1 << 0,
  OVK_CONNECTIVITY_EVENT_FLAGS_DESTROY = 1 << 1,
  OVK_CONNECTIVITY_EVENT_FLAGS_RESIZE_M = 1 << 2,
  OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_M_EXTENTS = 1 << 3,
  OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_M_COORDS = 1 << 4,
  OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_M_INTERP_COEFS = 1 << 5,
  OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_M_DESTINATIONS = 1 << 6,
  OVK_CONNECTIVITY_EVENT_FLAGS_RESIZE_N = 1 << 7,
  OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_N_POINTS = 1 << 8,
  OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_N_SOURCES = 1 << 9,
  OVK_CONNECTIVITY_EVENT_FLAGS_ALL_EDITS =
    OVK_CONNECTIVITY_EVENT_FLAGS_RESIZE_M |
    OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_M_EXTENTS |
    OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_M_COORDS |
    OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_M_INTERP_COEFS |
    OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_M_DESTINATIONS |
    OVK_CONNECTIVITY_EVENT_FLAGS_RESIZE_N |
    OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_N_POINTS |
    OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_N_SOURCES,
  OVK_CONNECTIVITY_EVENT_FLAGS_ALL =
    OVK_CONNECTIVITY_EVENT_FLAGS_CREATE |
    OVK_CONNECTIVITY_EVENT_FLAGS_DESTROY |
    OVK_CONNECTIVITY_EVENT_FLAGS_RESIZE_M |
    OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_M_EXTENTS |
    OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_M_COORDS |
    OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_M_INTERP_COEFS |
    OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_M_DESTINATIONS |
    OVK_CONNECTIVITY_EVENT_FLAGS_RESIZE_N |
    OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_N_POINTS |
    OVK_CONNECTIVITY_EVENT_FLAGS_EDIT_N_SOURCES
} ovk_connectivity_event_flags;

static inline bool ovkValidConnectivityEventFlags(ovk_connectivity_event_flags EventFlags) {

  return EventFlags >= OVK_CONNECTIVITY_EVENT_FLAGS_NONE && EventFlags <=
    OVK_CONNECTIVITY_EVENT_FLAGS_ALL;

}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
constexpr inline ovk_connectivity_event_flags operator|(ovk_connectivity_event_flags Left,
  ovk_connectivity_event_flags Right) {
  return ovk_connectivity_event_flags(int(Left) | int(Right));
}
constexpr inline ovk_connectivity_event_flags operator&(ovk_connectivity_event_flags Left,
  ovk_connectivity_event_flags Right) {
  return ovk_connectivity_event_flags(int(Left) & int(Right));
}
constexpr inline ovk_connectivity_event_flags operator^(ovk_connectivity_event_flags Left,
  ovk_connectivity_event_flags Right) {
  return ovk_connectivity_event_flags(int(Left) ^ int(Right));
}
constexpr inline ovk_connectivity_event_flags operator~(ovk_connectivity_event_flags EventFlags) {
  return ovk_connectivity_event_flags(~int(EventFlags));
}
inline ovk_connectivity_event_flags operator|=(ovk_connectivity_event_flags &Left,
  ovk_connectivity_event_flags Right) {
  return Left = Left | Right;
}
inline ovk_connectivity_event_flags operator&=(ovk_connectivity_event_flags &Left,
  ovk_connectivity_event_flags Right) {
  return Left = Left & Right;
}
inline ovk_connectivity_event_flags operator^=(ovk_connectivity_event_flags &Left,
  ovk_connectivity_event_flags Right) {
  return Left = Left ^ Right;
}
#endif

#endif
