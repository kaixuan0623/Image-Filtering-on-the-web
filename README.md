# Image Filtering on the web
Description: This program build a small web-server that allows user to run the image filters from my previous project on custom images
using only their web browser.

This program uses sockets to listen for HTTP requests, and construct an appropriate HTTP response.

We parse the first line of the HTTP request that contains the type of the request (GET or POST), and make a response correspondingly.

This program is intended for practicing and gaining hands-on experiences on the concept of inter-process communication across a network 
using sockets.
