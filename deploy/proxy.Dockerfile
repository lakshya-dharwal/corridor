# Terminates TLS and serves the demo page. API and alert routes go to the
# C++ process. The certificate is generated for localhost; browsers will warn.
FROM nginx:1.27-alpine

RUN apk add --no-cache openssl \
    && mkdir -p /etc/nginx/certs \
    && openssl req -x509 -nodes -newkey rsa:2048 -days 825 \
        -keyout /etc/nginx/certs/key.pem \
        -out /etc/nginx/certs/cert.pem \
        -subj "/CN=localhost"

COPY deploy/nginx.conf /etc/nginx/nginx.conf
COPY web/ /usr/share/nginx/html/
