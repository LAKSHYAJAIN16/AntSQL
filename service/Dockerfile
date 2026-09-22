# AntSQL Cloud: zero runtime dependencies, so no npm install step.
FROM node:22-alpine
WORKDIR /app
COPY package.json ./
COPY server ./server
COPY sdk ./sdk
COPY website ./website
ENV NODE_ENV=production \
    PORT=8080 \
    DATA_DIR=/data
# /data must be a persistent volume: it holds every replica's WAL and the
# API key store. Without one, a redeploy wipes the database.
VOLUME ["/data"]
EXPOSE 8080
# Runs as root: Fly volumes mount root-owned, so a non-root user couldn't
# write the WAL without an entrypoint chown step.
CMD ["node", "server/index.js"]
