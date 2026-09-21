#pragma once

#include "alpha.h"
#include "zeta.h"

#include <project/runtime/public.h>

#include <vector>

namespace sample::formatting {

using namespace domain;

////////////////////////////////////////////////////////////////////////////////

struct Channel
    : public ReferenceBase
{
    virtual void Read() = 0;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace sample::formatting
