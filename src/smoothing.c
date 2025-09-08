#include <math.h>
int Smoothing(int nYSize, int nXSize, int *dx, int *dy, int n, float *pixelArray, float *waterArray, float *res, double noDataValue)
{
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
            for (int d = 0; d < n; d++)
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
};