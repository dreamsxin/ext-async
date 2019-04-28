FROM php:7.3.4-cli

RUN apt-get update && apt-get install -y build-essential libssl-dev libxml2-dev autoconf bison bash automake libtool

COPY . /tmp/ext-async

WORKDIR /tmp/ext-async

RUN phpize --clean \
    && phpize

RUN ./configure
RUN make install -B
RUN echo "extension=\"async.so\"" >> /usr/local/etc/php/conf.d/async.ini

RUN make test-coverage
RUN php -v
RUN php -m
