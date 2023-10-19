FROM alpine:3.15.4

RUN apk update && apk add build-base gcc protobuf-dev

COPY . /app/

WORKDIR "/app"

RUN make

EXPOSE 1025

# CMD tail -f /dev/null
