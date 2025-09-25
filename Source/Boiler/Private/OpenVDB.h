#pragma once

THIRD_PARTY_INCLUDES_START
__pragma(warning(disable: 4706))
#undef check /* <- Otherwise we cannot compile... */
#include "openvdb/openvdb.h"
#include "openvdb/Grid.h"
#include "openvdb/tools/Interpolation.h"
#include "openvdb/tools/FastSweeping.h"
#include <openvdb/tools/GridTransformer.h>
#include <openvdb/tools/Filter.h>
#undef UpdateResource /* <- Windows header included somewhere... */
THIRD_PARTY_INCLUDES_END
