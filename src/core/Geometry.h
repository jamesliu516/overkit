// Copyright (c) 2020 Matthew J. Smith and Overkit contributors
// License: MIT (http://opensource.org/licenses/MIT)

#ifndef OVK_CORE_GEOMETRY_H_INCLUDED
#define OVK_CORE_GEOMETRY_H_INCLUDED

#include <ovk/core/Global.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  OVK_GEOMETRY_TYPE_UNIFORM,
  OVK_GEOMETRY_TYPE_ORIENTED_UNIFORM,
  OVK_GEOMETRY_TYPE_RECTILINEAR,
  OVK_GEOMETRY_TYPE_ORIENTED_RECTILINEAR,
  OVK_GEOMETRY_TYPE_CURVILINEAR
} ovk_geometry_type;

static inline bool ovkValidGeometryType(ovk_geometry_type GeometryType) {

  switch (GeometryType) {
  case OVK_GEOMETRY_TYPE_UNIFORM:
  case OVK_GEOMETRY_TYPE_ORIENTED_UNIFORM:
  case OVK_GEOMETRY_TYPE_RECTILINEAR:
  case OVK_GEOMETRY_TYPE_ORIENTED_RECTILINEAR:
  case OVK_GEOMETRY_TYPE_CURVILINEAR:
    return true;
  default:
    return false;
  }

}

#ifdef __cplusplus
}
#endif

#endif
