#ifndef _BYTEORDER_H_
#define _BYTEORDER_H_

#include "swab.h"

# define le64_to_cpu(x)  swab64((x))
# define cpu_to_le64(x)  swab64((x))
# define le32_to_cpu(x)  swab32((x))
# define cpu_to_le32(x)  swab32((x))
# define le16_to_cpu(x)  swab16((x))
# define cpu_to_le16(x)  swab16((x))

#endif /* _BYTEORDER_H_ */
