// main.c
#include "cpl_conv.h"
#include "gdal.h"
#include "gdalShortcut.h"
#include "smoothing.h"
#include "transformation.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, const char *argv[]) {
  if (argc < 11) {
    fprintf(stderr,
            "Usage: %s <dem.tif> <landuse.tif> <output.tif> "
            "<curah_hujan_mm_total> <pumpInLat> <pumpInLon> <pumpOutLat> "
            "<pumpOutLon> <pumpCapacity_m3_per_hr> <pumpThreshold_m> "
            "<pumpRadius_m>\n",
            argv[0]);
    return 1;
  }

  const char *pszFilename = argv[1];
  const char *pszFilename2 = argv[2];
  const char *output = argv[3];
  float rainfall_total_mm = (float)atof(argv[4]);
  double pumpInLat = atof(argv[5]);
  double pumpInLon = atof(argv[6]);
  double pumpOutLat = atof(argv[7]);
  double pumpOutLon = atof(argv[8]);
  double pumpCapacity_m3_per_hr = atof(argv[9]);
  double pumpThres = atof(argv[10]);
  float pump_radius_m = (argc >= 12) ? atof(argv[11]) : 2.0f; // default 2 m

  int pump_radius_px = (int)ceil(pump_radius_m / gsd);
  if (pump_radius_px < 1)
    pump_radius_px = 1;

  printf("Pump radius: %.2f m → %d pixels\n", pump_radius_m, pump_radius_px);

  GDALAllRegister();

  Raster dem = OpenTiff(pszFilename, 0, -32767);
  if (!dem.dataset) {
    fprintf(stderr, "Failed to open DEM: %s\n", pszFilename);
    return 1;
  }
  float *elevArray = (float *)dem.pixelArray;

  Raster lahanData = OpenTiff(pszFilename2, 1, NULL);
  if (!lahanData.dataset) {
    fprintf(stderr, "Failed to open landuse: %s\n", pszFilename2);
    GDALClose(dem.dataset);
    return 1;
  }
  int *lahan = (int *)lahanData.pixelArray;

  int nXSize = dem.nXSize;
  int nYSize = dem.nYSize;
  if (nXSize <= 0 || nYSize <= 0) {
    fprintf(stderr, "Invalid raster size\n");
    GDALClose(dem.dataset);
    GDALClose(lahanData.dataset);
    return 1;
  }

  int hasNoData = 0;
  double noDataValue = GDALGetRasterNoDataValue(dem.band, &hasNoData);

  int hours = 10;
  int iter = 10;
  float gsd = 0.5f;
  float pixelArea = gsd * gsd;

  float *hyeto = (float *)malloc(sizeof(float) * hours);
  for (int i = 0; i < hours; i++) {
    hyeto[i] = rainfall_total_mm / (float)hours;
  }

  float infil_capacity_mm_per_hr[4] = {0.0f, 10.0f, 5.0f, 30.0f};

  size_t npix = (size_t)nXSize * (size_t)nYSize;
  float *water = (float *)CPLCalloc(npix, sizeof(float));
  float *tmp = (float *)CPLCalloc(npix, sizeof(float));
  if (!water || !tmp) {
    fprintf(stderr, "Memory allocation failed\n");
    CPLFree(water);
    CPLFree(tmp);
    GDALClose(dem.dataset);
    GDALClose(lahanData.dataset);
    return 1;
  }

  int pumpLocX[3] = {548, 602, -1};
  int pumpLocY[3] = {718, 612, -1};
  int outLocX[3] = {528, 613, -1};
  int outLocY[3] = {713, 614, -1};

  float pumpThreshold[3] = {0.6f, 0.87f, (float)pumpThres};
  float pumpCapacity_m3hr[3] = {75.0f / 1000.0f, 80.0f / 1000.0f,
                                (float)pumpCapacity_m3_per_hr};

  int pumpState[3] = {0, 0, 0};
  int pumpCooldown[3] = {0, 0, 0};
  const int pumpCooldownEpochs = 1;
  const float pumpHysteresisFrac = 0.1f;

  if (pumpInLat != 0.0 && pumpInLon != 0.0 && pumpOutLat != 0.0 &&
      pumpOutLon != 0.0) {
    int px, py, ox, oy;
    LatLonToPixel(dem.dataset, pumpInLat, pumpInLon, &px, &py);
    LatLonToPixel(dem.dataset, pumpOutLat, pumpOutLon, &ox, &oy);
    if (px >= 0 && px < nXSize && py >= 0 && py < nYSize) {
      pumpLocX[2] = px;
      pumpLocY[2] = py;
      outLocX[2] = ox;
      outLocY[2] = oy;
      pumpCapacity_m3hr[2] = (float)pumpCapacity_m3_per_hr;
      pumpThreshold[2] = (float)pumpThres;
    } else {
      fprintf(stderr,
              "Provided pump lat/lon outside DEM bounds, ignoring pump 3\n");
    }
  }

  FILE *pumpLog = fopen("pump_log.csv", "w");
  if (!pumpLog) {
    perror("Failed to open pump_log.csv");
    return 1;
  }
  fprintf(pumpLog, "epoch,iter,pump_id,px,py,water_level_m,threshold_on_m,"
                   "threshold_off_m,pumped_m,active\n");

  int dx[4] = {-1, 1, 0, 0};
  int dy[4] = {0, 0, -1, 1};
  int nDirs = 4;

  for (int epoch = 0; epoch < hours; epoch++) {
    float rain_mm = hyeto[epoch];
    float rain_m = rain_mm / 1000.0f;

    for (size_t i = 0; i < npix; i++) {
      if (hasNoData && elevArray[i] == (float)noDataValue)
        continue;
      if (isnan(elevArray[i]))
        continue;
      water[i] += rain_m;
    }

    float t = (float)epoch / (float)((hours > 1) ? (hours - 1) : 1);
    float decayFactor = fmaxf(0.1f, 1.0f - t * 0.9f);

    for (int it = 0; it < iter; it++) {
      memcpy(tmp, water, sizeof(float) * npix);

      for (int y = 1; y < nYSize - 1; y++) {
        for (int x = 1; x < nXSize - 1; x++) {
          int idx = y * nXSize + x;
          if (hasNoData && elevArray[idx] == (float)noDataValue)
            continue;
          if (isnan(elevArray[idx]))
            continue;

          float z = elevArray[idx] + water[idx];
          float total = 0.0f;
          float pot[4] = {0, 0, 0, 0};

          for (int d = 0; d < nDirs; d++) {
            int nx = x + dx[d];
            int ny = y + dy[d];
            int nidx = ny * nXSize + nx;
            if (hasNoData && elevArray[nidx] == (float)noDataValue)
              continue;
            if (isnan(elevArray[nidx]))
              continue;
            float zn = elevArray[nidx] + water[nidx];
            float diff = z - zn;
            if (diff > 0.0f) {
              pot[d] = diff;
              total += diff;
            }
          }

          if (total > 0.0f) {
            for (int d = 0; d < nDirs; d++) {
              if (pot[d] <= 0.0f)
                continue;
              int nx = x + dx[d];
              int ny = y + dy[d];
              int nidx = ny * nXSize + nx;
              float flow = (pot[d] / total) * water[idx];
              tmp[idx] -= flow;
              tmp[nidx] += flow;
            }
          }

          int kelas = lahan[idx];
          if (kelas < 0 || kelas > 3)
            kelas = 0;
          float infil_mmhr = infil_capacity_mm_per_hr[kelas] * decayFactor;
          float infil_m = infil_mmhr / 1000.0f;
          if (tmp[idx] >= infil_m)
            tmp[idx] -= infil_m;
          else
            tmp[idx] = 0.0f;
        }
      }

      memcpy(water, tmp, sizeof(float) * npix);

      for (int pid = 0; pid < 3; pid++) {
        int px = pumpLocX[pid], py = pumpLocY[pid];
        int ox = outLocX[pid], oy = outLocY[pid];
        if (px < 0 || py < 0 || ox < 0 || oy < 0)
          continue;
        if (px >= nXSize || py >= nYSize || ox >= nXSize || oy >= nYSize)
          continue;

        int pidx = py * nXSize + px;
        int oidx = oy * nXSize + ox;

        float thresh_on = pumpThreshold[pid];
        float thresh_off =
            pumpThreshold[pid] - pumpHysteresisFrac * pumpThreshold[pid];
        if (thresh_off < 0.0f)
          thresh_off = 0.0f;

        if (pumpCooldown[pid] > 0)
          pumpCooldown[pid]--;

        if (!pumpState[pid]) {
          if (water[pidx] > thresh_on && pumpCooldown[pid] == 0) {
            pumpState[pid] = 1;
            pumpCooldown[pid] = pumpCooldownEpochs * iter;
          }
        } else {
          if (water[pidx] < thresh_off && pumpCooldown[pid] == 0) {
            pumpState[pid] = 0;
            pumpCooldown[pid] = pumpCooldownEpochs * iter;
          }
        }

        float pumped_this_iter = 0.0f;
        if (pumpState[pid]) {
          float dt_hours = 1.0f / (float)iter;
          float pumpVolume_m3_per_iter = pumpCapacity_m3hr[pid] * dt_hours;
          float pumped_depth_m_per_iter = pumpVolume_m3_per_iter / pixelArea;

          // hitung pixel dalam radius
          int countPix = 0;
          for (int dyR = -pump_radius; dyR <= pump_radius; dyR++) {
            for (int dxR = -pump_radius; dxR <= pump_radius; dxR++) {
              int nx = px + dxR;
              int ny = py + dyR;
              if (nx < 0 || ny < 0 || nx >= nXSize || ny >= nYSize)
                continue;
              if (dxR * dxR + dyR * dyR > pump_radius * pump_radius)
                continue; // lingkaran
              countPix++;
            }
          }

          float pumped_this_iter = 0.0f;
          if (pumpState[pid] && countPix > 0) {
            float dt_hours = 1.0f / (float)iter;
            float pumpVolume_m3_per_iter = pumpCapacity_m3hr[pid] * dt_hours;
            float pumped_depth_m_per_iter =
                pumpVolume_m3_per_iter / (pixelArea * countPix);

            for (int dy_r = -pump_radius_px; dy_r <= pump_radius_px; dy_r++) {
              for (int dx_r = -pump_radius_px; dx_r <= pump_radius_px; dx_r++) {
                int nx = px + dx_r;
                int ny = py + dy_r;
                if (nx < 0 || nx >= nXSize || ny < 0 || ny >= nYSize)
                  continue;

                // optional: circular radius
                if (dx_r * dx_r + dy_r * dy_r > pump_radius_px * pump_radius_px)
                  continue;

                int nidx = ny * nXSize + nx;
                float remove = fminf(water[nidx], pumped_depth_m_per_iter);
                water[nidx] -= remove;

                // tambahkan volume ke titik out (sederhana: semua masuk ke out
                // pixel)
                water[oidx] += remove;
                pumped_this_iter += remove;
              }
            }
          }
        }

        fprintf(pumpLog, "%d,%d,%d,%d,%d,%.6f,%.3f,%.3f,%.6f,%d\n", epoch, it,
                pid, px, py, water[pidx], thresh_on, thresh_off,
                pumped_this_iter, pumpState[pid]);
      }
    }
  }

  float *res = (float *)CPLMalloc(sizeof(float) * npix);
  Smoothing(nYSize, nXSize, dx, dy, nDirs, elevArray, water, res, noDataValue);
  WriteTiff(dem.dataset, res, nXSize, nYSize, output);

  CPLFree(res);
  CPLFree(tmp);
  CPLFree(water);
  CPLFree(lahanData.pixelArray);
  GDALClose(dem.dataset);
  GDALClose(lahanData.dataset);

  fclose(pumpLog);
  free(hyeto);
  return 0;
}
