# Overview:
 A C++ client that reads tick data from a server and writes a valid JSON containing complete orderbook data.

# Compilation instructions:
 Project was originally compiled with gcc 12.2.0 on Windows 10. The client works with Windows Socket API, hence can only be compiled and used on Windows. The library w2_32 needs to be linked at compile time as follows:
 gcc client.cpp -lw2_32 -o client

# Features of the client:
 > Design: 
