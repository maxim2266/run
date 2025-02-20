FROM    alpine:latest AS builder
RUN     [ "apk", "add", "--no-cache", "gcc", "make", "musl-dev", "git" ]
WORKDIR /build
COPY    . .
RUN     [ "make", "clean", "static" ]

FROM       alpine:latest
COPY       --from=builder /build/run /bin/run
ENTRYPOINT ["/bin/run", "-g"]
