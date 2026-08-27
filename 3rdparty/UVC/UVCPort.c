#include <stddef.h>
#include <sys/time.h>

#include "UVCPort.h"

__attribute__((weak)) uint64_t getEpochTimestampInUs(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000 * 1000 + (uint64_t)(tv.tv_usec);
}
