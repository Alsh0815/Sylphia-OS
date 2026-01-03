#pragma once

class System
{
public:
    const static struct Version
    {
#if defined(SYLPH_VERSION_MAJOR) && defined(SYLPH_VERSION_MINOR) &&            \
    defined(SYLPH_VERSION_PATCH) && defined(SYLPH_VERSION_REVISION)
        static constexpr int Major = SYLPH_VERSION_MAJOR;
        static constexpr int Minor = SYLPH_VERSION_MINOR;
        static constexpr int Patch = SYLPH_VERSION_PATCH;
        static constexpr int Revision = SYLPH_VERSION_REVISION;
#else
#error "Version information is not defined."
#endif
    } Version;
    const static struct BuildInfo
    {
        const static struct Date
        {
#ifdef SYLPH_BUILD_DATE_YEAR
            static constexpr int Year = SYLPH_BUILD_DATE_YEAR;
#else
            static constexpr int Year = 0;
#endif
#ifdef SYLPH_BUILD_DATE_MONTH
            static constexpr int Month = SYLPH_BUILD_DATE_MONTH;
#else
            static constexpr int Month = 0;
#endif
#ifdef SYLPH_BUILD_DATE_DAY
            static constexpr int Day = SYLPH_BUILD_DATE_DAY;
#else
            static constexpr int Day = 0;
#endif
        } Date;
    } BuildInfo;

    const static char *ReleaseTypeToString();
};