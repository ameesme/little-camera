# Little Camera micro-blog server. Built from the repository root:
#
#   docker build -f server/Dockerfile -t little-camera-server .
#
# One stage: the server runs TypeScript directly through tsx (there is no
# bundling step to get wrong), and the only compiled piece is the pbm
# package, built here. node:sqlite is part of Node 22, so there are no
# native modules to compile.
FROM node:22-alpine

ENV NODE_ENV=production \
    PNPM_HOME=/pnpm \
    PATH=/pnpm:$PATH
RUN corepack enable

WORKDIR /app
COPY package.json pnpm-lock.yaml pnpm-workspace.yaml ./
COPY packages/pbm/package.json packages/pbm/
COPY server/package.json server/
RUN pnpm install --frozen-lockfile --prod=false

COPY packages/pbm packages/pbm
COPY server server
RUN pnpm --filter @little-camera/pbm build

ENV PORT=3000 \
    DATA_DIR=/data
VOLUME /data
EXPOSE 3000

CMD ["pnpm", "--filter", "@little-camera/server", "start"]
