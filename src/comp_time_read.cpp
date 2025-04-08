#include "comp_time_read.h"


template<>
float read_type<float>(char *v)
{
    return read_float(v);
}

template<>
double read_type<double>(char *v)
{
    return read_double(v);
}