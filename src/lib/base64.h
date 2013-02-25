/*
 **  These functions are from the Squid Http Proxy project.
 */
#ifndef S_BASE64_H
#define S_BASE64_H

#ifdef __cplusplus
extern "C"
{
#endif

        char *base64_decode(const char *p);
        const char *base64_encode(const char *decoded_str, int);

#ifdef __cplusplus
}
#endif

#endif
