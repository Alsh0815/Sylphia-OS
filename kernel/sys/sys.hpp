#pragma once

#ifndef __SYLPHIAOS_SYS_PRE_CONSTANTS__
#define __SYLPHIAOS_SYS_PRE_CONSTANTS__

#define RELEASE_TYPE__RELEASE 0
#define RELEASE_TYPE__PRE_RELEASE 1
#define RELEASE_TYPE__BETA 2
#define RELEASE_TYPE__ALPHA 3

#endif

class System
{
  public:
    const static struct Version
    {
        static constexpr int Major = 0;
        static constexpr int Minor = 5;
        static constexpr int Patch = 8;
        static constexpr int Revision = 1;
    } Version;
    const static struct BuildInfo
    {
        const static struct Date
        {
            static constexpr int Year = 2025;
            static constexpr int Month = 12;
            static constexpr int Day = 25;
        } Date;
        const static int ReleaseType = RELEASE_TYPE__ALPHA;
    } BuildInfo;

    const static char *ReleaseTypeToString();
};