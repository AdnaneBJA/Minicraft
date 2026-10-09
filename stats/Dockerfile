# The stats service in a container. Build from the repository root:
#   docker build -f stats/Dockerfile -t minicraft-stats .
FROM golang:1.26-alpine AS build
WORKDIR /src/stats
COPY stats/go.mod stats/go.sum ./
RUN go mod download
COPY stats/ ./
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /out/stats ./cmd/stats

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /out/stats /stats
# The game's sprite sheets, for the item and mob icons on the dashboard.
COPY assets/sprites /sprites
EXPOSE 8080
ENTRYPOINT ["/stats"]
