#pragma once

#ifdef DEBUG
#define DBGI(...) do { Serial.printf(__VA_ARGS__); } while (0)
#define DBG(...) do { Serial.printf(__VA_ARGS__); Serial.println(); } while (0)
// TODO: BETTER ERROR REPORTING
#define ERR(...) do { Serial.write("! ERR "); Serial.printf(__VA_ARGS__); Serial.println(); } while (0)
#else
#define DBGI(...) do { } while (0)
#define DBG(...) do { } while (0)
#define ERR(...) do { } while (0)
#endif

#ifdef DEBUG
inline void hex_dump(const char *prefix, const void *p, size_t size) {
    const char *ptr = reinterpret_cast<const char *>(p);
    DBGI("%s : [%d bytes] ", prefix, size);
    for(;size;++ptr,--size) {
        DBGI("%02x ", (uint8_t)*ptr);
    }
    DBG(".");
}
#else
inline void hex_dump(const char *prefix, const void *p, size_t size) {}
#endif

// TODO: Use variadic template to skip over temporary buffer usage for DBG macros

// Use this to make master more verbose
// * NOTE: it might run into timing issues with clients then
// #define VERBOSE
