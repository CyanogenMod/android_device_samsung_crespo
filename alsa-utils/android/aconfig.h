
#define DATADIR "/system/usr/share/alsa"

#define rindex strrchr
#define open64 open

//typedef int off64_t;

#undef __swab16
#define __swab16(x)  __arch__swab16((x))

#undef __swab32
#define __swab32(x)  __arch__swab32((x))

