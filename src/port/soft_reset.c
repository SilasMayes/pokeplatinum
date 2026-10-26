#include "port/soft_reset.h"

#include "system.h"

static BOOL sRequested = FALSE;

BOOL SoftReset_Allowed(void)
{
    return gSystem.inhibitReset == 0;
}

void SoftReset_Request(void)
{
    sRequested = TRUE;
}

BOOL SoftReset_TakeRequest(void)
{
    BOOL requested = sRequested;
    sRequested = FALSE;
    return requested;
}
