/*
 * pthread_setname_np и pthread_getname_np: Emscripten 6.0.10 объявляет их
 * в <pthread.h>, но в libc их нет. Thread.cpp из libslic3r называет ими
 * потоки. Имена нужны только для отладки, поэтому имя одного потока
 * запоминается, остальное принимается и игнорируется.
 * Слабые символы: если libc когда-нибудь их получит, победит её версия.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <string.h>

/* Как в Linux: 15 символов и завершающий ноль. */
static char current_name[16];

__attribute__((weak)) int pthread_setname_np(pthread_t thread, const char *name)
{
    (void)thread;
    if (name == NULL)
        return EINVAL;
    if (strlen(name) >= sizeof(current_name))
        return ERANGE;
    strcpy(current_name, name);
    return 0;
}

__attribute__((weak)) int pthread_getname_np(pthread_t thread, char *name, size_t len)
{
    (void)thread;
    if (name == NULL || len == 0)
        return EINVAL;
    if (strlen(current_name) >= len)
        return ERANGE;
    strcpy(name, current_name);
    return 0;
}
