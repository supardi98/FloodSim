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

typedef struct {
  int px, py;
  int ox, oy;
  float capacity_m3hr;
  float threshold;
  int radius_px;
  int state;
  int cooldown;
} Pump;

int parseFloatArray(const char *str, float **out, int *n) {
  char *s = strdup(str);
  int count = 0;
  for (char *p = s; *p; p++)
    if (*p == ',')
      count++;
  *n = count + 1;
  *out = (float *)malloc(sizeof(float) * (*n));
  int i = 0;
  char *token = strtok(s, ",");
  while (token) {
    (*out)[i++] = atof(token);
    token = strtok(NULL, ",");
  }
  free(s);
  return 0;
}

int main(int argc, const char *argv[]) {
  if (argc < 11) {
    fprintf(
        stderr,
        "Usage: %s <dem.tif> <landuse.tif> <output.tif> <curah_hujan_mm_total> "
        "<pumpInLat,...> <pumpInLon,...> <pumpOutLat,...> <pumpOutLon,...> "
        "<pumpCapacity_m3_per_hr,...> <pumpThreshold_m,...> "
        "<pumpRadius_m,...>\n",
        argv[0]);
    return 1;
  }

  const char *demFile = argv[1];
  const char *lahanFile = argv[2];
  const char *output = argv[3];
  float rainfall_total_mm = atof(argv[4]);

  float *inLat, *inLon, *outLat, *outLon, *capacities, *thresholds, *radii;
  int nPumps1, nPumps2, nPumps3, nPumps4, nPumps5, nPumps6, nPumps7;

  parseFloatArray(argv[5], &inLat, &nPumps1);
  parseFloatArray(argv[6], &inLon, &nPumps2);
  parseFloatArray(argv[7], &outLat, &nPumps3);
  parseFloatArray(argv[8], &outLon, &nPumps4);
  parseFloatArray(argv[9], &capacities, &nPumps5);
  parseFloatArray(argv[10], &thresholds, &nPumps6);

  int nPumps = nPumps1; // asumsi semua array sama panjang
  if (argc >= 12) {
    parseFloatArray(argv[11], &radii, &nPumps7);
  } else {
    nPumps7 = nPumps1; // set agar validasi tidak error
    radii = (float *)malloc(sizeof(float) * nPumps);
    for (int i = 0; i < nPumps; i++)
      radii[i] = 2.0f; // default 2 m
  }

  // Validasi semua array sama panjang
  if (nPumps1 != nPumps2 || nPumps1 != nPumps3 || nPumps1 != nPumps4 ||
      nPumps1 != nPumps5 || nPumps1 != nPumps6 || nPumps1 != nPumps7) {
    fprintf(stderr, "All pump arrays must have same length\n");
    return 1;
  }

  GDALAllRegister();

  Raster dem = OpenTiff(demFile, 0, -32767);
  if (!dem.dataset) {
    fprintf(stderr, "Failed to open DEM: %s\n", demFile);
    return 1;
  }
  float *elevArray = (float *)dem.pixelArray;

  Raster lahanData = OpenTiff(lahanFile, 1, NULL);
  if (!lahanData.dataset) {
    fprintf(stderr, "Failed to open landuse: %s\n", lahanFile);
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

  Pump *pumps = (Pump *)malloc(sizeof(Pump) * nPumps);
  for (int i = 0; i < nPumps; i++) {
    int px, py, ox, oy;
    LatLonToPixel(dem.dataset, inLat[i], inLon[i], &px, &py);
    LatLonToPixel(dem.dataset, outLat[i], outLon[i], &ox, &oy);
    if (px < 0 || px >= nXSize || py < 0 || py >= nYSize || ox < 0 ||
        ox >= nXSize || oy < 0 || oy >= nYSize) {
      fprintf(stderr, "Pump %d outside DEM bounds, ignored\n", i);
      pumps[i].px = -1;
      continue;
    }
    pumps[i].px = px;
    pumps[i].py = py;
    pumps[i].ox = ox;
    pumps[i].oy = oy;
    pumps[i].capacity_m3hr = capacities[i];
    pumps[i].threshold = thresholds[i];
    pumps[i].radius_px = (int)ceil(radii[i] / gsd);
    if (pumps[i].radius_px < 1)
      pumps[i].radius_px = 1;
    pumps[i].state = 0;
    pumps[i].cooldown = 0;
  }

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

  const int pumpCooldownEpochs = 1;
  const float pumpHysteresisFrac = 0.1f;

  for (int epoch = 0; epoch < hours; epoch++) {
    float rain_mm = hyeto[epoch];
    float rain_m = rain_mm / 1000.0f;

    // tambahkan hujan ke semua pixel
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

      // water flow + infiltration
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

          // alirkan ke 4 arah
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

          // infiltrasi
          int kelas = lahan[idx];
          if (kelas < 0 || kelas > 3)
            kelas = 0;
          float infil_mmhr = infil_capacity_mm_per_hr[kelas] * decayFactor;
          float infil_m = infil_mmhr / 1000.0f;
          tmp[idx] = fmaxf(0.0f, tmp[idx] - infil_m);
        }
      }

      memcpy(water, tmp, sizeof(float) * npix);

      // loop pompa dinamis
      for (int pid = 0; pid < nPumps; pid++) {
        Pump *p = &pumps[pid];
        if (p->px < 0)
          continue; // pompa tidak valid
        int pidx = p->py * nXSize + p->px;
        int oidx = p->oy * nXSize + p->ox;

        float thresh_on = p->threshold;
        float thresh_off = p->threshold * (1.0f - pumpHysteresisFrac);
        if (thresh_off < 0.0f)
          thresh_off = 0.0f;

        if (p->cooldown > 0)
          p->cooldown--;

        if (!p->state) {
          if (water[pidx] > thresh_on && p->cooldown == 0) {
            p->state = 1;
            p->cooldown = pumpCooldownEpochs * iter;
          }
        } else {
          if (water[pidx] < thresh_off && p->cooldown == 0) {
            p->state = 0;
            p->cooldown = pumpCooldownEpochs * iter;
          }
        }

        float pumped_this_iter = 0.0f;

        if (p->state) {
          // hitung jumlah pixel dalam radius
          int countPix = 0;
          for (int dyR = -p->radius_px; dyR <= p->radius_px; dyR++) {
            for (int dxR = -p->radius_px; dxR <= p->radius_px; dxR++) {
              int nx = p->px + dxR;
              int ny = p->py + dyR;
              if (nx < 0 || ny < 0 || nx >= nXSize || ny >= nYSize)
                continue;
              if (dxR * dxR + dyR * dyR > p->radius_px * p->radius_px)
                continue;
              countPix++;
            }
          }

          if (countPix > 0) {
            float dt_hours = 1.0f / (float)iter;
            float pumpVolIter = p->capacity_m3hr * dt_hours;
            float pumped_depth_m = pumpVolIter / (pixelArea * countPix);

            for (int dyR = -p->radius_px; dyR <= p->radius_px; dyR++) {
              for (int dxR = -p->radius_px; dxR <= p->radius_px; dxR++) {
                int nx = p->px + dxR;
                int ny = p->py + dyR;
                if (nx < 0 || ny < 0 || nx >= nXSize || ny >= nYSize)
                  continue;
                if (dxR * dxR + dyR * dyR > p->radius_px * p->radius_px)
                  continue;

                int nidx = ny * nXSize + nx;
                float remove = fminf(water[nidx], pumped_depth_m);
                water[nidx] -= remove;
                water[oidx] += remove; // masuk ke titik out
                pumped_this_iter += remove;
              }
            }
          }
        }

        fprintf(pumpLog, "%d,%d,%d,%d,%d,%.6f,%.3f,%.3f,%.6f,%d\n", epoch, it,
                pid, p->px, p->py, water[pidx], thresh_on, thresh_off,
                pumped_this_iter, p->state);
      } // end pump loop
    } // end iter loop
  } // end epoch loop

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
  free(inLat);
  free(inLon);
  free(outLat);
  free(outLon);
  free(capacities);
  free(thresholds);
  free(radii);
  free(pumps);
  free(hyeto);
  return 0;
}
