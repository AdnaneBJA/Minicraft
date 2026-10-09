# The monitoring probe in a container. Build from the repository root:
#   docker build -f loadtest/Dockerfile -t minicraft-probe .
FROM golang:1.26-alpine AS build
WORKDIR /src/loadtest
COPY loadtest/go.mod loadtest/go.sum ./
RUN go mod download
COPY loadtest/ ./
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /out/probe ./cmd/probe

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /out/probe /probe
EXPOSE 9100
ENTRYPOINT ["/probe"]
