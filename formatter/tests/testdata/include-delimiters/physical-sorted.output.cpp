#include "../include-delimiters/local.h"
#include "./alias/../local.h"
#include "./local.h"
#include "formatter/tests/testdata/include-delimiters/child/../local.h"
#include "local.h"

#include "local-link.h"

#include "./child/local-link.h"

#include <./missing.h>

#include <./alias/header.h>

#include <./child/dangling.h>
#include <../include-delimiters/child/header.h>
#include <formatter/tests/testdata/include-delimiters/child/header.h>

#include <external-link.h>
#include <missing.h>
#include <stdio.h>

#include <child>
#include <vector>
