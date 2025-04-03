#pragma once
#include <cstring>
#include <string>
#include <cstdint>
#ifdef __FreeBSD__
#include <sys/endian.h>
#endif
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>


#define htobe16(x) OSSwapHostToBigInt16(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)

#define htobe32(x) OSSwapHostToBigInt32(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)

#define htobe64(x) OSSwapHostToBigInt64(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)
#endif
struct char_size
{
    char *data;
    int consumed_size;
    int max_size;
    char *start_data;
};

struct packet
{
	unsigned long size;
	std::size_t buf_size;
	char *data;
	char *start_data;
	bool operator==(const packet& other) const 
	{
		if ( size == other.size && buf_size == other.buf_size)
		{
			if (memcmp(start_data, other.start_data, buf_size) == 0)
				return true;
		}
		return false;
	}
    packet& operator=(packet rhs) 
    { 
        size = rhs.size;
        buf_size = rhs.buf_size;
        data = rhs.data;
        start_data = rhs.start_data;
        
        return *this;
    }
};


double read_double(char *buf);
float read_float(char *buf);
char *mem_dup(char *buf, int size);