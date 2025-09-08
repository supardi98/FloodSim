#include "gdal.h"
#include "cpl_conv.h"

typedef struct
{
    GDALDatasetH dataset;
    GDALRasterBandH band;
    int nXSize;
    int nYSize;
    void *pixelArray;
} Raster;

void WriteTiff(GDALDatasetH hDataset, float *pixelArray, int nXSize, int nYSize, char *output)
{
    GDALDriverH driver = GDALGetDriverByName("GTiff");

    GDALDatasetH outputDataset = GDALCreate(driver, output, nXSize, nYSize, 1, GDT_Float32, NULL);

    // Optional: Copy GeoTransform and Projection from original
    double geoTransform[6];
    GDALGetGeoTransform(hDataset, geoTransform);
    GDALSetGeoTransform(outputDataset, geoTransform);

    const char *proj = GDALGetProjectionRef(hDataset);
    GDALSetProjection(outputDataset, proj);

    // Step 5: Write modified data to the new file
    GDALRasterBandH outputBand = GDALGetRasterBand(outputDataset, 1);
    GDALSetRasterNoDataValue(outputBand, -32767);
    // GDALSetRasterNoDataValue(outputBand, 0);
    if (GDALRasterIO(outputBand, GF_Write, 0, 0, nXSize, nYSize, pixelArray, nXSize, nYSize, GDT_Float32, 0, 0))
    {
        CPLFree(pixelArray);
        GDALClose(hDataset);
        GDALClose(outputDataset);
    }
    GDALClose(outputDataset);
}

Raster OpenTiff(char *filename, int type, int noDataVal)
{
    // type
    // 0 = float
    // 1 = int
    Raster result;
    result.pixelArray = NULL;
    result.dataset = GDALOpen(filename, GA_ReadOnly);
    // Get the first band
    result.band = GDALGetRasterBand(result.dataset, 1);

    result.nXSize = GDALGetRasterBandXSize(result.band);
    result.nYSize = GDALGetRasterBandYSize(result.band);
    if (noDataVal != NULL)
    {
        GDALSetRasterNoDataValue(result.band, noDataVal);
    }

    printf("Raster size: %d cols x %d rows\n", result.nXSize, result.nYSize);

    if (type == 0)
    {
        result.pixelArray = CPLMalloc(result.nXSize * result.nYSize * sizeof(float));
        if (GDALRasterIO(result.band, GF_Read, 0, 0,
                         result.nXSize, result.nYSize,
                         result.pixelArray, result.nXSize, result.nYSize,
                         GDT_Float32, 0, 0) != CE_None)
        {
            fprintf(stderr, "Error: GDALRasterIO failed (float)\n");
            CPLFree(result.pixelArray);
            result.pixelArray = NULL;
        }
    }
    else if (type == 1)
    {
        result.pixelArray = CPLMalloc(result.nXSize * result.nYSize * sizeof(int));
        if (GDALRasterIO(result.band, GF_Read, 0, 0,
                         result.nXSize, result.nYSize,
                         result.pixelArray, result.nXSize, result.nYSize,
                         GDT_Int32, 0, 0) != CE_None)
        {
            fprintf(stderr, "Error: GDALRasterIO failed (int)\n");
            CPLFree(result.pixelArray);
            result.pixelArray = NULL;
        }
    }
    return result;
}