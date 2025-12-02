#include "string.hpp"

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, int n)
{
    for (int i = 0; i < n; i++)
    {
        if (s1[i] != s2[i])
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        if (s1[i] == 0)
            return 0;
    }
    return 0;
}

int strlen(const char *s)
{
    int len = 0;
    while (s[len])
    {
        len++;
    }
    return len;
}

char *strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d)
    {
        d++;
    }
    while (*src)
    {
        *d++ = *src++;
    }
    *d = 0;
    return dest;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while (*src)
    {
        *d++ = *src++;
    }
    *d = 0;
    return dest;
}