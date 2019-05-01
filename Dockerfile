FROM php:7.3.4-zts

RUN apt-get update && apt-get install -y build-essential libssl-dev libxml2-dev autoconf bison bash automake libtool

COPY . /usr/local/ext-async
WORKDIR /usr/local/ext-async

RUN phpize --clean
RUN phpize
RUN ./configure
RUN make install -B

RUN cat ./defaults.ini >> /usr/local/etc/php/conf.d/async.ini

RUN php -v
RUN php -m

CMD ["/bin/sh"]
