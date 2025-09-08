#include "gdal.h"
#include "cpl_conv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gdalShortcut.h"
#include "smoothing.h"
#include "transformation.h"
int main(int argc, const char *argv[])
{
    // You need 9 arguments: program + 8 params
    // printf("%d", argc);
    // if (argc != 4 || argc > 9)
    // {
    //     fprintf(stderr, "Usage: %s <input_dem.tif> <input_landuse.tif> <output_sim.tif> <curah_hujan_mm> <lat_pump_in> <lon_pump_in> <lat_pump_out> <lon_pump_out>\n", argv[0]);
    //     return EINVAL;
    // }

    const char *pszFilename = argv[1];  // DEM
    const char *pszFilename2 = argv[2]; // Landuse
    const char *output = argv[3];
    const float arg2 = (float)atof(argv[4]); // Curah hujan in mm
    double pumpInLat = atof(argv[5]);
    double pumpInLon = atof(argv[6]);
    double pumpOutLat = atof(argv[7]);
    double pumpOutLon = atof(argv[8]);
    double pumpCapacity = atof(argv[9]);
    double pumpThres = atof(argv[10]);

    // Initialize GDAL
    GDALAllRegister();

    // Open datasets (assumes OpenTiff returns a struct with dataset, band, nXSize, nYSize and pixelArray)
    Raster dem = OpenTiff(pszFilename, 0, -32767);
    if (!dem.dataset)
    {
        fprintf(stderr, "Failed to open DEM: %s\n", pszFilename);
        return 1;
    }
    float *pixelArray = (float *)dem.pixelArray;

    Raster lahanData = OpenTiff(pszFilename2, 1, NULL);
    if (!lahanData.dataset)
    {
        fprintf(stderr, "Failed to open landuse: %s\n", pszFilename2);
        GDALClose(dem.dataset);
        return 1;
    }
    int *lahan = (int *)lahanData.pixelArray;

    int nXSize = dem.nXSize;
    int nYSize = dem.nYSize;
    if (nXSize <= 0 || nYSize <= 0)
    {
        fprintf(stderr, "Invalid raster size\n");
        GDALClose(dem.dataset);
        GDALClose(lahanData.dataset);
        return 1;
    }

    int hasNoData = 0;
    double noDataValue = GDALGetRasterNoDataValue(dem.band, &hasNoData);
    printf("hasNoData=%d\n", hasNoData);

    float gsd = 0.5f;                     // meters per pixel (verify)
    float curah_hujan_m = arg2 / 1000.0f; // mm -> m
    float pixelArea = gsd * gsd;
    // th per epoch will be curah_hujan_m / hours

    // Allocate and zero arrays (use CPLCalloc to zero)
    size_t npix = (size_t)nXSize * (size_t)nYSize;
    float *waterArray = (float *)CPLCalloc(npix, sizeof(float));
    float *tempWaterArray = (float *)CPLCalloc(npix, sizeof(float));
    if (!waterArray || !tempWaterArray)
    {
        fprintf(stderr, "Memory allocation for water array failed\n");
        if (waterArray)
            CPLFree(waterArray);
        if (tempWaterArray)
            CPLFree(tempWaterArray);
        GDALClose(dem.dataset);
        GDALClose(lahanData.dataset);
        return 1;
    }

    printf("Inisialisasi curah hujan\n");

    int iter = 10;
    int hours = 10;

    float infil_mm_per_hr[4] = {0.0f, 10.0f, 5.0f, 30.0f};

    // neighbor offsets: you only use first 4 directions (N,S,W,E)
    int dx[8] = {-1, 1, 0, 0, -1, -1, 1, 1};
    int dy[8] = {0, 0, -1, 1, -1, 1, -1, 1};
    int n = 4;

    // Simulation loop: add rainfall evenly over hours (stores depth in meters)
    for (int epoch = 0; epoch < hours; epoch++)
    {
        // add rainfall depth for this epoch (m)
        float addDepthPerEpoch = curah_hujan_m / (float)hours; // depth (m) per pixel

        for (size_t i = 0; i < npix; i++)
        {
            // skip nodata/elevation NaN pixels
            if (hasNoData && pixelArray[i] == (float)noDataValue)
                continue;
            if (isnan(pixelArray[i]))
                continue;
            waterArray[i] += addDepthPerEpoch;
        }

        printf("Epoch : %d\n", epoch);
        for (int it = 0; it < iter; it++)
        {
            // copy current depths into temp for updates
            memcpy(tempWaterArray, waterArray, sizeof(float) * npix);

            for (int y = 1; y < nYSize - 1; y++)
            {
                for (int x = 1; x < nXSize - 1; x++)
                {
                    int idx = y * nXSize + x;

                    if (hasNoData && pixelArray[idx] == (float)noDataValue)
                        continue;
                    if (isnan(pixelArray[idx]))
                        continue;

                    float elev = pixelArray[idx] + waterArray[idx];
                    float totalFlow = 0.0f;
                    float flows[4] = {0};

                    // compute flow potential to 4 neighbors
                    for (int d = 0; d < n; d++)
                    {
                        int nx = x + dx[d];
                        int ny = y + dy[d];
                        int nIdx = ny * nXSize + nx;

                        if (hasNoData && pixelArray[nIdx] == (float)noDataValue)
                            continue;
                        if (isnan(pixelArray[nIdx]))
                            continue;

                        float neighElev = pixelArray[nIdx] + waterArray[nIdx];
                        float diff = elev - neighElev;
                        if (diff > 0.0f)
                        {
                            flows[d] = diff;
                            totalFlow += diff;
                        }
                    }

                    if (totalFlow > 0.0f)
                    {
                        for (int d = 0; d < n; d++)
                        {
                            int nx = x + dx[d];
                            int ny = y + dy[d];
                            int nIdx = ny * nXSize + nx;

                            // distribute water depth proportionally (keeps units as depth m)
                            float flowAmount = (flows[d] / totalFlow) * waterArray[idx];
                            tempWaterArray[idx] -= flowAmount;
                            tempWaterArray[nIdx] += flowAmount;
                        }
                    }

                    // infiltration: infil_mm_per_hr is mm/hr -> convert to m per this epoch (hours steps)
                    int kelas = lahan[idx];
                    if (kelas < 0 || kelas > 3)
                        kelas = 0;                                                               // safety clamp
                    float infil_m_per_epoch = (infil_mm_per_hr[kelas] / 1000.0f) / (float)hours; // converting mm/hr to m per epoch (approx)
                    if (tempWaterArray[idx] >= infil_m_per_epoch)
                    {
                        tempWaterArray[idx] -= infil_m_per_epoch;
                    }
                    else
                    {
                        tempWaterArray[idx] = 0.0f;
                    }
                }
            }

            // write back updated water depths
            memcpy(waterArray, tempWaterArray, sizeof(float) * npix);
        }
    }

    int pumpLocX[3] = {548, 602};
    int pumpLocY[3] = {718, 612};
    int outLocX[3] = {528, 613};
    int outLocY[3] = {713, 614};
    float pumpThreshold[3] = {0.6, 0.87};
    float pumpPerHour_m[3] = {75.0f / 1000.0f, 80.0f / 1000.0f}; // example: 75 mm/hr -> m/hr (adjust if you meant m3/hr)
    bool pumpState[3] = {false, false, false};
    if (pumpInLat != 0)
    {
        LatLonToPixel(dem.dataset, pumpInLat, pumpInLon, &pumpLocX[2], &pumpLocY[2]);
        LatLonToPixel(dem.dataset, pumpOutLat, pumpOutLon, &outLocX[2], &outLocY[2]);
        pumpPerHour_m[2] = pumpCapacity / 1000.0f;
        pumpThreshold[2] = pumpThres;
    }

    for (int idPump = 0; idPump < 2; idPump++)
    {
        int px = pumpLocX[idPump], py = pumpLocY[idPump];
        int outx = outLocX[idPump], outy = outLocY[idPump];
        if (px < 0 || px >= nXSize || py < 0 || py >= nYSize)
            continue;
        if (outx < 0 || outx >= nXSize || outy < 0 || outy >= nYSize)
            continue;

        int pidx = py * nXSize + px;
        int oidx = outy * nXSize + outx;

        if (waterArray[pidx] > 0.6f && !pumpState[idPump])
        {

            waterArray[pidx] = 0.0f;

            waterArray[oidx] += pumpPerHour_m[idPump];
            pumpState[idPump] = true;
        }
    }

    // Continue flow relaxation after pumps
    for (int it = 0; it < iter * 5; it++)
    {
        memcpy(tempWaterArray, waterArray, sizeof(float) * npix);

        for (int y = 1; y < nYSize - 1; y++)
        {
            for (int x = 1; x < nXSize - 1; x++)
            {
                int idx = y * nXSize + x;

                if (hasNoData && pixelArray[idx] == (float)noDataValue)
                    continue;
                if (isnan(pixelArray[idx]))
                    continue;

                float elev = pixelArray[idx] + waterArray[idx];
                float totalFlow = 0.0f;
                float flows[4] = {0};

                for (int d = 0; d < n; d++)
                {
                    int nx = x + dx[d];
                    int ny = y + dy[d];
                    int nIdx = ny * nXSize + nx;
                    if (hasNoData && pixelArray[nIdx] == (float)noDataValue)
                        continue;
                    if (isnan(pixelArray[nIdx]))
                        continue;

                    float neighElev = pixelArray[nIdx] + waterArray[nIdx];
                    float diff = elev - neighElev;
                    if (diff > 0.0f)
                    {
                        flows[d] = diff;
                        totalFlow += diff;
                    }
                }

                if (totalFlow > 0.0f)
                {
                    for (int d = 0; d < n; d++)
                    {
                        int nx = x + dx[d];
                        int ny = y + dy[d];
                        int nIdx = ny * nXSize + nx;

                        float flowAmount = (flows[d] / totalFlow) * waterArray[idx];
                        tempWaterArray[idx] -= flowAmount;
                        tempWaterArray[nIdx] += flowAmount;
                    }
                }
            }
        }

        // write back updated water depths
        memcpy(waterArray, tempWaterArray, sizeof(float) * npix);
    }

    // allocate result (smoothed)
    float *res = (float *)CPLMalloc(sizeof(float) * npix);
    if (!res)
    {
        fprintf(stderr, "Failed to allocate result array\n");
        CPLFree(tempWaterArray);
        CPLFree(waterArray);
        GDALClose(dem.dataset);
        GDALClose(lahanData.dataset);
        return 1;
    }

    // Call smoothing (check Smoothing signature in your header)
    Smoothing(nYSize, nXSize, dx, dy, n, pixelArray, waterArray, res, noDataValue);
    printf("Hasil akhir\n");

    // cleanup
    CPLFree(tempWaterArray);
    CPLFree(waterArray);

    // Write result (assumes WriteTiff takes dataset, buffer, xsize, ysize, outname)
    WriteTiff(dem.dataset, res, nXSize, nYSize, output);

    CPLFree(res);

    // free landuse pixel array and close datasets (depends on OpenTiff ownership semantics)
    CPLFree(lahanData.pixelArray);
    GDALClose(dem.dataset);
    GDALClose(lahanData.dataset);

    return 0;
}
