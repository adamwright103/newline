#ifndef FETCHER_H
#define FETCHER_H
#include <stdbool.h>

// Changed from fetcher_start() to a blocking, single-run function
bool fetch_daily_info(void);

#endif