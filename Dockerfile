FROM    alpine:latest AS builder
RUN     apk add --no-cache gcc make musl-dev git
WORKDIR /build
COPY    . .
RUN     [ "make", "clean", "static" ]

FROM scratch
WORKDIR /bin
COPY --from=builder /build/run .
ENTRYPOINT ["/bin/run", "-g"]
