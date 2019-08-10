FROM alpine:edge AS build
# pugixml-dev is in edge/testing
RUN apk add --no-cache --repository http://dl-3.alpinelinux.org/alpine/edge/testing/ \
  cmake \  
  clang \
  gcc \  
  make \
  binutils \
  boost-dev \
  build-base \
  crypto++-dev \
  lua-dev \
  luajit-dev \
  mariadb-connector-c-dev \
  pugixml-dev \
  sqlite-dev \
  sqlite-libs \
  unixodbc-dev \
  postgresql-dev

COPY cmake /usr/src/3884/cmake/
COPY src /usr/src/3884/src/
COPY CMakeLists.txt /usr/src/3884/
WORKDIR /usr/src/3884/build
RUN cmake .. && make

FROM alpine:edge
# pugixml-dev is in edge/testing
RUN apk add --no-cache --repository http://dl-3.alpinelinux.org/alpine/edge/testing/ \
  boost-iostreams \
  boost-system \ 
  lua-dev \
  luajit \
  crypto++ \
  mariadb-connector-c \
  pugixml \
  sqlite-dev \
  sqlite-libs \
  unixodbc-dev \
  postgresql-dev

RUN ln -s /usr/lib/libcryptopp.so /usr/lib/libcryptopp.so.5.6
COPY --from=build /usr/src/3884/build/tfs /bin/tfs
COPY data /srv/data/
COPY LICENSE README.md *.dist *.sql key.pem config.* theforgottenserver.* /srv/

EXPOSE 7171 7172
WORKDIR /srv
VOLUME /srv
ENTRYPOINT ["/bin/tfs"]
