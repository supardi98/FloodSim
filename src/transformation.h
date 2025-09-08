#ifndef transformation
#define transformation
#include "gdal.h"

int LatLonToPixel(GDALDatasetH hDataset, double lat, double lon, int *col, int *row);
#endif