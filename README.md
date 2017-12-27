# tcp-proxy-with-caching


proxy side:

1. make 
2. ./proxy <cache_size>

client side:
bash -c export http_proxy=<hostname>:<port> && wget <url to cache>

