#ifndef _STD_STRING_H_
#define _STD_STRING_H_

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

#endif // _STD_STRING_H_