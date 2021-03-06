// Copyright (c) 2020 Matthew J. Smith and Overkit contributors
// License: MIT (http://opensource.org/licenses/MIT)

#ifndef OVK_CORE_STATE_COMPONENT_H_INCLUDED
#define OVK_CORE_STATE_COMPONENT_H_INCLUDED

#include <ovk/core/Global.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  OVK_STATE_EVENT_FLAGS_NONE = 0,
  OVK_STATE_EVENT_FLAGS_CREATE = 1 << 0,
  OVK_STATE_EVENT_FLAGS_DESTROY = 1 << 1,
  OVK_STATE_EVENT_FLAGS_EDIT_FLAGS = 1 << 2,
  OVK_STATE_EVENT_FLAGS_ALL =
    OVK_STATE_EVENT_FLAGS_CREATE |
    OVK_STATE_EVENT_FLAGS_DESTROY |
    OVK_STATE_EVENT_FLAGS_EDIT_FLAGS
} ovk_state_event_flags;

static inline bool ovkValidStateEventFlags(ovk_state_event_flags EventFlags) {

  return EventFlags >= OVK_STATE_EVENT_FLAGS_NONE && EventFlags <= OVK_STATE_EVENT_FLAGS_ALL;

}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
constexpr inline ovk_state_event_flags operator|(ovk_state_event_flags Left, ovk_state_event_flags
  Right) {
  return ovk_state_event_flags(int(Left) | int(Right));
}
constexpr inline ovk_state_event_flags operator&(ovk_state_event_flags Left, ovk_state_event_flags
  Right) {
  return ovk_state_event_flags(int(Left) & int(Right));
}
constexpr inline ovk_state_event_flags operator^(ovk_state_event_flags Left, ovk_state_event_flags
  Right) {
  return ovk_state_event_flags(int(Left) ^ int(Right));
}
constexpr inline ovk_state_event_flags operator~(ovk_state_event_flags EventFlags) {
  return ovk_state_event_flags(~int(EventFlags));
}
inline ovk_state_event_flags operator|=(ovk_state_event_flags &Left, ovk_state_event_flags Right) {
  return Left = Left | Right;
}
inline ovk_state_event_flags operator&=(ovk_state_event_flags &Left, ovk_state_event_flags Right) {
  return Left = Left & Right;
}
inline ovk_state_event_flags operator^=(ovk_state_event_flags &Left, ovk_state_event_flags Right) {
  return Left = Left ^ Right;
}
#endif

#endif
