// Intentionally repeatable: isolate the Windows SDK near/far aliases between
// responsibilities. minwindef.h defines FAR as far and NEAR as near; flatten
// their empty aliases before removing the lower-case names used by engine code.
#ifdef _WIN32
#ifdef FAR
#undef FAR
#define FAR
#endif
#ifdef NEAR
#undef NEAR
#define NEAR
#endif
#undef near
#undef far
#endif
