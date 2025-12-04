#include "sys.hpp"

const char *System::ReleaseTypeToString()
{
    switch (BuildInfo.ReleaseType)
    {
        case RELEASE_TYPE__RELEASE:
            return "release";
        case RELEASE_TYPE__PRE_RELEASE:
            return "pre";
        case RELEASE_TYPE__BETA:
            return "beta";
        case RELEASE_TYPE__ALPHA:
            return "alpha";
        default:
            return "unknown";
    }
}