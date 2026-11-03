/* adflib/src/config.h -- the build configuration ADFlib's autotools would generate, for the firmware (RV32IM, newlib
 * nano) and the host tool tools/afstool: both C libraries provide strnlen, strndup and stpncpy; chmod, backtrace and
 * mempcpy are not used. */
#define HAVE_STRNLEN 1
#define HAVE_STRNDUP 1
#define HAVE_STPNCPY 1
