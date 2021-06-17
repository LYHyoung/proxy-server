Proxy Server
============

<br />

Proxy Server implementation
---------------------------
<p align="center"><img src="https://user-images.githubusercontent.com/37611500/122348985-b853d080-cf86-11eb-9bfc-3c466b227b04.PNG" height="300" width="450">

+ Made in Ubuntu 16.04
+ Make logfile
  + Write HIt and MISS data(URL) in logfile.
  + Write Run time, sub process count in logfile.
+ Make cache file
  + Made by hashing URL
    + HTTP request URL
    + Write Message to cache file when MISS
    + Get Message from cache file when HIT
    
<br />

Run
---
<pre>
make
</pre>

<br />

Open Browser
------------
#### Chrome
<pre>
google-chrome --proxy-server="IP Address:PIN number"
</pre>
#### FireFox
Setting -> General -> Network Settings -> Settings... -> Manual proxy configuration -> Set "HTTP Proxy", "Port" -> OK

<br/>

Made Directory Structure :
---------------------
<pre>
<code>
├── proxy_cache
│   ├── Makefile
│   ├── proxy_cache.c
│   └── proxy_cache
├── cache
│   ├── cache file 1
│   ├── cache file 2
...
│   └── cache file N
└── logfile
    └── logfile.txt
</code>
</pre>

