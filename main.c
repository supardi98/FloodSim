#include "gdal.h"
#include "cpl_conv.h" // For CPLMalloc/CPLFree
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>

int WriteTiff(GDALDatasetH hDataset, float *pixelArray, int nXSize, int nYSize, char *output);
float KernelOps(float *kernel);
int main(int argc, const char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <input_raster_file>\n", argv[0]);
        return EINVAL;
    }

    const char *pszFilename = argv[1];
    const float arg2 = atof(argv[2]);
    printf("%f", arg2);

    // Initialize GDAL
    GDALAllRegister();
    // Open the dataset
    GDALDatasetH hDataset = GDALOpen(pszFilename, GA_ReadOnly);
    if (!hDataset)
    {
        fprintf(stderr, "Error: Could not open file '%s'\n", pszFilename);
        return 1;
    }
    // Get the first band
    GDALRasterBandH hBand = GDALGetRasterBand(hDataset, 1);

    GDALSetRasterNoDataValue(hBand, -32767);
    if (!hBand)
    {
        fprintf(stderr, "Error: Could not get raster band\n");
        GDALClose(hDataset);
        return 1;
    }
    // Get raster dimensions dynamically
    const int nXSize = GDALGetRasterBandXSize(hBand);
    const int nYSize = GDALGetRasterBandYSize(hBand);
    printf("Raster size: %d cols x %d rows\n", nXSize, nYSize);
    // Check for empty raster
    if (nXSize <= 0 || nYSize <= 0)
    {
        fprintf(stderr, "Error: Raster has invalid dimensions\n");
        GDALClose(hDataset);
        return 1;
    }

    // Allocate memory for the entire band
    float *pixelArray = (float *)CPLMalloc(nXSize * nYSize * sizeof(float));
    if (!pixelArray)
    {
        fprintf(stderr, "Error: Memory allocation failed\n");
        GDALClose(hDataset);
        return 1;
    }

    // Read the entire band into the array
    if (GDALRasterIO(hBand, GF_Read,
                     0, 0,             // Start at top-left corner (x=0, y=0)
                     nXSize, nYSize,   // Read full raster
                     pixelArray,       // Buffer to store data
                     nXSize, nYSize,   // Buffer dimensions (same as raster)
                     GDT_Float32,      // Data type (adjust if needed)
                     0, 0) != CE_None) // Pixel/line spacing
    {
        fprintf(stderr, "Error: Could not read raster data\n");
        CPLFree(pixelArray);
        GDALClose(hDataset);
        return 1;
    }
    int hasNoData = 0;
    double noDataValue = GDALGetRasterNoDataValue(hBand, &hasNoData);
    // 1. hitung nilai tentangga
    // 2. jika elevasi + air tetangga < elevasi + air titik sekarang
    // 3. msaukin ke array dengan nilai volume / pixel eligible
    // 0 1 0 kalau horizontal && vertikal > diagonal sedangkan diagonal < elev + air
    // 1 0 1 skip
    // 0 1 0
    // 4. curah hujan itu Liter/m2 => ketinggian air / meter2
    // 5. convert ke air/pixel

    float gsd = 0.5;
    // gsd = 8.2884099563;
    float curah_hujan = arg2 / 100; // m
    float waterPerPixel = curah_hujan;

    // Alokasi array air
    float *waterArray = (float *)CPLMalloc(nXSize * nYSize * sizeof(float));
    float *tempWaterArray = (float *)CPLMalloc(nXSize * nYSize * sizeof(float));
    if (!waterArray || !tempWaterArray)
    {
        fprintf(stderr, "Memory allocation for water array failed\n");
        CPLFree(pixelArray);
        GDALClose(hDataset);
        return 1;
    }

    // Inisialisasi curah hujan ke semua pixel
    printf("Inisialiasi curah hujan\n");

    // Simulasi aliran air
    int iter = 20;
    int hours = 30;
    int dx[4] = {-1, 1, 0, 0};
    int dy[4] = {0, 0, -1, 1};
    for (int epoch = 0; epoch < hours; epoch++)
    {
        for (int i = 0; i < nYSize * nXSize; i++)
        {
            if (!isnan(pixelArray[i]) && pixelArray[i] != noDataValue)
            {
                waterArray[i] += (waterPerPixel / hours);
            }
        }
        /* code */
        printf("Epoch : %d\n", epoch);
        for (int it = 0; it < iter; it++)
        {
            // printf("Iter : %d\n", it);
            memcpy(tempWaterArray, waterArray, sizeof(float) * nXSize * nYSize);

            for (int y = 1; y < nYSize - 1; y++)
            {
                for (int x = 1; x < nXSize - 1; x++)
                {
                    int idx = y * nXSize + x;
                    if (x < 0 || y < 0 || x >= nXSize || y >= nYSize)
                    {
                        continue;
                    };

                    if (isnan(pixelArray[idx]) || pixelArray[idx] == noDataValue)
                        continue;

                    float elev = pixelArray[idx] + waterArray[idx];
                    float totalFlow = 0;
                    float flows[4] = {0};

                    // Hitung aliran ke tetangga
                    for (int d = 0; d < 4; d++)
                    {
                        int nx = x + dx[d];
                        int ny = y + dy[d];
                        int nIdx = ny * nXSize + nx;
                        if (isnan(pixelArray[nIdx]) || pixelArray[nIdx] == noDataValue)
                            continue;

                        float neighElev = pixelArray[nIdx] + waterArray[nIdx];
                        float diff = elev - neighElev;
                        if (diff > 0)
                        {
                            flows[d] = diff;
                            totalFlow += diff;
                        }
                    }

                    if (totalFlow > 0)
                    {
                        for (int d = 0; d < 4; d++)
                        {
                            int nx = x + dx[d];
                            int ny = y + dy[d];
                            int nIdx = ny * nXSize + nx;
                            float flowAmount = (flows[d] / totalFlow) * waterArray[idx]; // 0.5 = distribusi sebagian
                            tempWaterArray[idx] -= flowAmount;
                            tempWaterArray[nIdx] += flowAmount;
                        }
                    }
                }
            }
            // Swap array
            memcpy(waterArray, tempWaterArray, sizeof(float) * nXSize * nYSize);
        }
    }
    float *res = (float *)CPLMalloc(nXSize * nYSize * sizeof(float));
    for (int y = 1; y < nYSize - 1; y++)
    {
        for (int x = 0; x < nXSize - 1; x++)
        {
            int idx = y * nXSize + x;
            if (x < 0 || y < 0 || x >= nXSize || y >= nYSize)
            {
                continue;
            };

            if (isnan(pixelArray[idx]) || pixelArray[idx] == noDataValue)
                continue;

            float val = 0;
            float count = 0;
            for (int d = 0; d < 4; d++)
            {
                int nx = x + dx[d];
                int ny = y + dy[d];
                int nIdx = ny * nXSize + nx;
                if (isnan(pixelArray[nIdx]) || pixelArray[nIdx] == noDataValue)
                    break;
                if (waterArray[idx] <= waterArray[nIdx])
                {
                    count++;
                    val += waterArray[nIdx];
                }
            }
            if (count > 0)
            {
                res[idx] = val / count;
            }
            else
            {
                res[idx] = waterArray[idx];
            }
        }
    }

    printf("Hasil akhir \n");
    CPLFree(tempWaterArray);
    CPLFree(waterArray);
    WriteTiff(hDataset, res, nXSize, nYSize, "result/result.tif");
    CPLFree(res);
    GDALClose(hDataset);
    return 0;
}

int WriteTiff(GDALDatasetH hDataset, float *pixelArray, int nXSize, int nYSize, char *output)
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
    if (GDALRasterIO(outputBand, GF_Write, 0, 0, nXSize, nYSize, pixelArray, nXSize, nYSize, GDT_Float32, 0, 0))
    {
        CPLFree(pixelArray);
        GDALClose(hDataset);
        GDALClose(outputDataset);
    }
    GDALClose(outputDataset);
    return 0;
}
