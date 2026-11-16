#pragma once
#include "Backend.h"
#include "Context.h"
#include "DrawList.h"
#include "Export.h"
#include "Style.h"
#include "Types.h"

// Plain C entry point: safe to call from any language / compiler.
extern "C" FLUID_API const char* FluidUI_Version();