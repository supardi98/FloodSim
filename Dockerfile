# Stage 1: build main + install dependencies
FROM ghcr.io/osgeo/gdal:ubuntu-small-latest AS builder

ARG NODE_ENV

# Install Node 20 + dos2unix
RUN apt-get update && apt-get install -y curl dos2unix build-essential time \
 && curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
 && apt-get install -y nodejs \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

ENV NODE_ENV=${NODE_ENV}

COPY package.json package-lock.json* ./  
RUN npm install --production

COPY . .

RUN dos2unix build.sh run.sh
RUN chmod +x build.sh run.sh

# Build main
RUN ./build.sh

# -----------------------------
# Stage 2: final image
FROM ghcr.io/osgeo/gdal:ubuntu-small-latest

# Install Node 20 runtime + dos2unix
RUN apt-get update && apt-get install -y curl dos2unix \
 && curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
 && apt-get install -y nodejs \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy hasil build + Node modules dari builder
COPY --from=builder /app /app

EXPOSE 4000
CMD ["node", "server.js"]
