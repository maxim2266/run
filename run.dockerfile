FROM	alpine:latest AS builder
RUN		[ "apk", "add", "--no-cache", "gcc", "make", "musl-dev", "git" ]
WORKDIR	/build
COPY	. .
RUN		[ "make", "clean-local", "static" ]

FROM	alpine:latest
COPY	--from=builder /build/run /bin/run

ENV	USER=somebody GROUP=boys

RUN	addgroup --gid 1000 "$GROUP" \
&&	adduser --disabled-password --gecos "" --ingroup "$GROUP" --no-create-home --uid 1000 "$USER"

USER		somebody
ENTRYPOINT	[ "/bin/run" ]
