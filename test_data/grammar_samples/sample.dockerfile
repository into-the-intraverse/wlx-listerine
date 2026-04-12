FROM ubuntu:22.04 AS builder

ENV APP_HOME=/app
WORKDIR ${APP_HOME}

RUN apt-get update && \
    apt-get install -y --no-install-recommends gcc

COPY src/ ./src/
COPY Makefile .

RUN make build

FROM ubuntu:22.04
COPY --from=builder /app/bin /usr/local/bin
EXPOSE 8080
CMD ["myapp", "--port", "8080"]
