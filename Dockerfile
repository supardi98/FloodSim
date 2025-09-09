FROM node:20-alpine

# Install gdal tools
RUN apk add --no-cache gdal gdal-dev python3 py3-pip bash

WORKDIR /app

COPY package.json package-lock.json* ./  
RUN npm install --production

COPY . .

# Pastikan run.sh executable
RUN chmod +x run.sh

EXPOSE 4000

CMD ["node", "server.js"]
