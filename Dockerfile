FROM    alpine:latest AS builder
RUN     apk add --no-cache build-base musl-dev git
WORKDIR /build
COPY    . .
RUN     [ "make", "clean", "static" ]

FROM scratch
WORKDIR /bin
COPY --from=builder /build/run .
ENTRYPOINT ["/bin/run", "-g"]
