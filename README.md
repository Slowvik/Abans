# Overview:
 A C++ client that reads tick data from a server and writes a valid JSON containing complete orderbook data.

# Compilation instructions:
 Project was originally compiled with gcc 12.2.0 on Windows 10. The client works with Windows Socket API, hence can only be compiled and used on Windows. The library w2_32 needs to be linked at compile time as follows:
 gcc client.cpp -lw2_32 -o client

# Features of the client:
 ## Design:
 Connections are handled by instances of the class ClientApplication. The whole connection lifecycle, from starting up the Windows Socket API to creating a socket to closing the socket and cleaning up WSA is handled by the instance. The constructor starts up WSA, creates a socket and tries to connect to the server. The Destructor closes the connection. The connection can also be closed in case errors are encountered using the member function cleanupAndClose(). Currently, ClientApplication is not a singleton class, and there may be merit in making it a singleton class.
 
 ##Algorithm:
 Inside main(), we do the following:
 1. Create an instance of ClientApplication and call member function sendAllPacketsRequest() in a loop till at least one packet is received from the server. In case the server is not running, there is a 5 second wait before trying to connect again. This can be made incremental (upto a certain limit) if required.
 2. Inside sendAllPacketsRequest(), we expect packets to arrive in order, starting with sequence number 1. In case one or more packets are missing, we push the missing sequence numbers onto a stack.
 3. In a second loop, we create a new instance of ClientApplication and call sendSpecificPacketRequests() till the stack of missing sequence numbers is empty. The same timeout of 5 seconds in case of an inactive connection applies.
 4. After we have all the packets, we sort the tick_vector (which has been storing all the packet information) such that the first element (index 0) corresponds to sequence number 1.
 5. Finally, we call the JSON writer function writeJSON() to generate a JSON file containing the market data we have received.

 ## Points to note:
  1. If the connection drops, or some other error is encountered while receiving data, we close the socket, clean up WSA, and request a new connection.
  2. The program generates a log file called client_log.txt in the same folder where the executable is present. It has two different kinds of messages: INFO, for general information (such as when some data has been received), and ERRORs with codes, designed to point out exactly where an error was encountered.
  3. static_assert has been used to assert buffer sizes, etc. at compile-time. This ensures that there is no bad reading of data received from the server (unless the server itself sends bad data). Exceptions have been used to check validity of received data (For instance, Symbol must be 4 uppercase english letters). If an exception is thrown, the client disconnects and the program is designed to exit. The server throws a reconnection error when an this happens, but that is for the server to handle. Server crashes caused by client disconnections cannot be handled by the client alone. In short, since TCP guarantees stream validity, it is not possible for data to be incorrect provided there is no bad reading (related to buffer/packet sizes) and provided that the server does not send bad data (such as a negative integer for unit quantity).
 ##Additional notes:
 1. After trying out a few open-source JSON libraries for C++, it was decided to write our own JSON writer, since the data we have is very specific (market data) and can be done accurately and quickly with very few lines of code.
 2. Care was taken while using the left shift (<<) operator with character arrays. The name of a character array is a pointer to the first element, and the left shift operator will print the entire continuous block of memory that contain characters. This means that char a[4] and char b, when occupying continuous blocks of memory, will be joined together if the we try to use std::cout<<a. This is specific to character array names (char*) and is not an issue with other kinds of pointers (such as int* for example).
 3. Care was taken while dealing with server-side disconnects. It is not possible to reopen a half-closed socket, hence it is necessary to go through the entire WSA startup - socket creation - connect - socket close - WSA cleanup cycle every time we have to send a new request. If the server closes the connection, the client follows up by closing the socket and cleaning up WSA before attempting a new request.
