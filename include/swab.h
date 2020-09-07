#ifndef _REISERFS_SWAB_H_
#define _REISERFS_SWAB_H_

#if (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)
/* Stolen from GCC4.+ */
#define swab16(x) __builtin_bswap16(x)
#define swab32(x) __builtin_bswap32(x)
#define swab64(x) __builtin_bswap64(x)
#else
/* Stolen from linux/include/linux/byteorder/swab.h */
static inline __u16 swab16(__u16 x)
{
return ((__u16)( \
               (((__u16)(x) & (__u16)0x00ffU) << 8) |
               (((__u16)(x) & (__u16)0xff00U) >> 8) ));
}
static inline __u32 swab32(__u32 x)
{
return ((__u32)( \
               (((__u32)(x) & (__u32)0x000000ffUL) << 24) |
               (((__u32)(x) & (__u32)0x0000ff00UL) <<  8) |
               (((__u32)(x) & (__u32)0x00ff0000UL) >>  8) |
               (((__u32)(x) & (__u32)0xff000000UL) >> 24) ));
}
static inline __u64 swab64(__u64 x)
{
return ((__u64)( \
               (__u64)(((__u64)(x) & (__u64)0x00000000000000ffULL) << 56) |
               (__u64)(((__u64)(x) & (__u64)0x000000000000ff00ULL) << 40) |
               (__u64)(((__u64)(x) & (__u64)0x0000000000ff0000ULL) << 24) |
               (__u64)(((__u64)(x) & (__u64)0x00000000ff000000ULL) <<  8) |
               (__u64)(((__u64)(x) & (__u64)0x000000ff00000000ULL) >>  8) |
               (__u64)(((__u64)(x) & (__u64)0x0000ff0000000000ULL) >> 24) |
               (__u64)(((__u64)(x) & (__u64)0x00ff000000000000ULL) >> 40) |
               (__u64)(((__u64)(x) & (__u64)0xff00000000000000ULL) >> 56) ));
}
#endif

#endif /* _REISERFS_SWAB_H_ */
