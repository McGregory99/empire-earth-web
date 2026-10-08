/* Part of the differential test harness for "TCP Protocol.dll"; see harness.h. */
#include "harness.h"

/* ------------------------------------------------------------------------ */
/* Transcript output                                                         */
/* ------------------------------------------------------------------------ */

void line(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    putchar('\n');
    fflush(stdout);
}

const char *hex(const uint8_t *bytes, size_t count)
{
    static char rotating[8][1200];
    static unsigned next;
    char *out = rotating[next++ % 8];
    size_t used = 0;
    if (count == 0) {
        strcpy(out, "(empty)");
        return out;
    }
    for (size_t i = 0; i < count && used + 4 < sizeof rotating[0]; i++) {
        used += (size_t)sprintf(out + used, i ? " %02x" : "%02x", bytes[i]);
    }
    return out;
}

/* Never print a real ephemeral port: it differs on every run. */
const char *port_text(unsigned port)
{
    static char rotating[8][24];
    static unsigned next;
    char *out = rotating[next++ % 8];
    if (port >= 49152u || (port >= 1024u && port != 33334u && port != 33335u && port < 33000u)) {
        strcpy(out, "<ephemeral>");
    } else {
        sprintf(out, "%u", port);
    }
    return out;
}


const char *chars_of(const UString *s) 
{
    return (s && s->chars) ? s->chars : "(null)";
}

const char *wchars_of(const UWideString *s) 
{
    return (s && s->chars) ? s->chars : "(null)";
}

const char *ipv4_text(const char *text)
{
    unsigned a, b, c, d;
    char tail;
    return (text && sscanf(text, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4 &&
            a < 256 && b < 256 && c < 256 && d < 256) ? "dotted-quad" : "NOT-AN-ADDRESS";
}
