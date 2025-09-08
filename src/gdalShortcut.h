#ifndef gdalShortcut
#define gdalShortcut

#include "gdal.h"
typedef struct
{
    GDALDatasetH dataset;
    GDALRasterBandH band;
    int nXSize;
    int nYSize;
    void *pixelArray;
} Raster;
void WriteTiff(GDALDatasetH hDataset, float *pixelArray, int nXSize, int nYSize, char *output);
Raster OpenTiff(char *filename, int type, int noDataVal);
#endif
