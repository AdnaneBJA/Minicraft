# minicraft-server in a container. Build from the repository root:
#   docker build -f server/Dockerfile -t minicraft-server .
# The build also runs the tests; a failing test fails the image.
FROM debian:trixie-slim AS build
RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ cmake ninja-build git ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMINICRAFT_BUILD_CLIENT=OFF \
    && cmake --build build \
    && ctest --test-dir build --output-on-failure

FROM debian:trixie-slim
COPY --from=build /src/build/minicraft-server /usr/local/bin/minicraft-server
EXPOSE 7777
USER nobody
ENTRYPOINT ["minicraft-server", "7777"]
