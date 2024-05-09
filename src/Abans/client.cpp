#include <iostream>
#include <fstream>
#include <iomanip>
#include <cassert>
#include <winsock2.h>
#include <vector>
#include <stack>
#include <algorithm>
#include <thread>
#include <chrono>
#include <stdexcept>


namespace client
{
    struct Tick
    {
        char symbol[4];
        char buysell_indicator;
        int32_t quantity;
        int32_t price;
        int32_t packet_sequence;

        friend std::ostream& operator<<(std::ostream& output, const Tick& tick)
        {
            output<<
            "Symbol: "<<tick.symbol[0]<<tick.symbol[1]<<tick.symbol[2]<<std::setw(10) << std::left<<tick.symbol[3]<<
            "Buy/Sell Indicator: "<<std::setw(10) << std::left <<tick.buysell_indicator<<
            "Quantity: "<<std::setw(10) << std::left <<tick.quantity<<
            "Price: "<<std::setw(10) << std::left<<tick.price<<
            "Sequence Number: "<<std::setw(10) << std::left<<tick.packet_sequence<<"\n";
            return output;
        }        
    };

    const char log_file_name[] = "client_log.txt";    
    std::vector<Tick> tick_vector; 
    std::stack<int> missed_packets;

    char byteToBuySellIndicator(unsigned char* buffer, int start_pos)
    {
        return (char)(buffer[start_pos+4]);
    }

    int32_t byteToQuantity(unsigned char* buffer, int start_pos)
    {
        int32_t quantity = int(
                            (int32_t)(buffer[start_pos+5]) << 24 |
                            (int32_t)(buffer[start_pos+6]) << 16 |
                            (int32_t)(buffer[start_pos+7]) << 8 |
                            (int32_t)(buffer[start_pos+8]));

        return quantity;
    }

    int32_t byteToPrice(unsigned char* buffer, int start_pos)
    {
        int32_t price = int(
                            (int32_t)(buffer[start_pos+9]) << 24 |
                            (int32_t)(buffer[start_pos+10]) << 16 |
                            (int32_t)(buffer[start_pos+11]) << 8 |
                            (int32_t)(buffer[start_pos+12]));

        return price;
    }

    int32_t byteToPacketSequence(unsigned char* buffer, int start_pos)
    {
        int32_t packet_sequence = int(
                            (int32_t)(buffer[start_pos+13]) << 24 |
                            (int32_t)(buffer[start_pos+14]) << 16 |
                            (int32_t)(buffer[start_pos+15]) << 8 |
                            (int32_t)(buffer[start_pos+16]));

        return packet_sequence;
    }    

    bool tickIntegrityCheck(const Tick& t)
    {        
        
        try
        {
            if(!(t.symbol[0]>=char(65) && t.symbol[0]<=char(90)) ||
                !(t.symbol[1]>=char(65) && t.symbol[1]<=char(90)) ||
                !(t.symbol[2]>=char(65) && t.symbol[2]<=char(90)) ||
                !(t.symbol[3]>=char(65) && t.symbol[3]<=char(90)))
            {
                throw std::range_error("Symbol should be uppercase english letters");
            }
            else if(!(t.buysell_indicator == 'B' || t.buysell_indicator == 'S'))
            {
                throw std::invalid_argument("Buy/Sell indicator should be either B or S");
            }
            else if(!(t.quantity > 0))
            {
                throw std::range_error("Quantity should be a non-zero positive integer");
            }
            else if(!(t.price > 0))
            {
                throw std::range_error("Price should be a non-zero positive integer");
            }
            else if(!(t.packet_sequence > 0))
            {
                throw std::range_error("Sequence number should be a non-zero positive integer");
            }
        }
        catch(const std::exception& e)
        {
            std::cerr <<"Error: "<< e.what() << '\n';
            return false;
        }

        return true;
    }

    class ClientApplication
    {
        private:
            const static int request_buffer_size = 2;  
            const static int received_buffer_size = 170;
            const static int packet_size_default = 17; 
            int seq_num;
            char payload[2];
            bool WSAstartup_successful;
            bool connection_successful;

            WSADATA client_data;
            SOCKET ABX_socket;
            struct sockaddr_in ABX_server;

            std::ofstream log;
        
        public:
            ClientApplication()
            {
                static_assert(packet_size_default == 17);
                static_assert(received_buffer_size >= 17);
                static_assert(request_buffer_size >= 2);
                seq_num = 1;
                
                //Open the log stream:
                log.open(log_file_name, std::ios::app | std::ios::out);
                if(log.fail())
                {
                    std::cout<<"Could not open log file for writing, proceeding without logging..."<<std::endl;
                }

                //Startup WSA
                WSADATA client_data;
                int WSA_return = WSAStartup(MAKEWORD(2,2), &client_data); //version 2.2

                if(WSA_return!=0)
                {
                    //std::cout<<"WSAStartup failed"<<std::endl;
                    log<<"ERROR00: WSAStartup failed"<<std::endl;
                    WSAstartup_successful = false;
                    return;
                }
                //std::cout<<"WSAStartup successful"<<std::endl;
                log<<"INFO: WSAStartup successful"<<std::endl;
                WSAstartup_successful = true;

                //Create a new socket
                ABX_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

                //Populate the server struct with server details
                ABX_server.sin_family = AF_INET;
                ABX_server.sin_addr.s_addr = inet_addr("127.0.0.1");
                ABX_server.sin_port = htons(3000);

                //Connect to the ABX server
                int ABX_connection_status = connect(ABX_socket, (SOCKADDR*)&ABX_server, sizeof(ABX_server));
                if(ABX_connection_status == SOCKET_ERROR)
                {
                    std::cout<<"Error connecting to ABX server, waiting 5 seconds..."<<std::endl;
                    log<<"ERROR01: Error connecting to ABX server, waiting 5 seconds..."<<std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
                    connection_successful = false;
                    return;
                }
                //std::cout<<"Successfully connected to ABX server"<<std::endl;
                log<<"INFO: Successfully connected to ABX server"<<std::endl;
                connection_successful = true;
            }

            ClientApplication(const ClientApplication& c) = delete; //No copy construction allowed          

            void cleanupAndClose()
            {
                if(connection_successful)
                {
                    connection_successful = false;
                    //Close the connection
                    int close_return_ABX = closesocket(ABX_socket);
                    if(close_return_ABX == SOCKET_ERROR)
                    {
                        //std::cout<<"Error closing ABX socket"<<std::endl;
                        log<<"ERROR02: Error closing ABX socket"<<std::endl;
                        return;
                    }
                    //std::cout<<"Closed ABX socket"<<std::endl;
                    log<<"INFO: Closed ABX socket"<<std::endl;                  
                }

                //Cleanup WSA
                if(WSAstartup_successful)
                {
                    WSAstartup_successful = false;
                    int WSA_cleanup_return = WSACleanup();
                    if(WSA_cleanup_return == SOCKET_ERROR)
                    {
                        //std::cout<<"WSACleanup error"<<std::endl;
                        log<<"ERROR03: WSACleanup error"<<std::endl;
                        return;
                    }
                    //std::cout<<"WSACleanup successful"<<std::endl;
                    log<<"INFO: WSACleanup successful"<<std::endl;                   
                }

                if(!log.fail())
                {
                    log.close();
                }                
            }

            ~ClientApplication()
            {
                cleanupAndClose();
            }

        public:

            void sendAllPacketsRequest()
            {
                if(!WSAstartup_successful || !connection_successful)
                {
                    return;
                }
                //Preparing payload    
                payload[0] = 1;
                payload[1] = 0;

                //Sending request to ABX server
                int send_return = send(ABX_socket, (char*)payload, request_buffer_size, 0);
                if(send_return == SOCKET_ERROR)
                {
                    //std::cout<<"Sending request for all packets failed"<<std::endl;
                    log<<"ERROR10: Sending request for all packets failed"<<std::endl;
                    cleanupAndClose();
                    return;
                }
                //std::cout<<"Sending request for all packets successful"<<std::endl;
                log<<"INFO: Sending request for all packets successful"<<std::endl;

                //Receive the data
                unsigned char ABX_buffer[received_buffer_size];
                while(true)
                {
                    //std::cout<<"Reading next tick..."<<std::endl;
                    log<<"INFO: Reading next tick..."<<std::endl;

                    int ABX_buffer_size = recv(ABX_socket, (char*)ABX_buffer, received_buffer_size, 0);

                    //Stops receiving when all data is sent
                    if(ABX_buffer_size==SOCKET_ERROR || ABX_buffer_size == 0)
                    {
                        if(!tick_vector.empty())
                        {
                            //std::cout<<"Data received and connection closed by ABX"<<std::endl;
                            log<<"INFO: Data received and connection closed by ABX"<<std::endl;
                            break;
                        }
                        else
                        {
                            //std::cout<<"No data received, socket error while reading next tick"<<std::endl;
                            log<<"ERROR11: No data received, socket error while reading next tick"<<std::endl;
                            cleanupAndClose();
                            break;
                        }
                    }
                    

                    //Loop to extract packets  
                    int start = 0;          
                    while(ABX_buffer_size>=packet_size_default)
                    {    
                        //std::cout<<"Parsing received tick..."<<std::endl; 
                        log<<"INFO: Parsing received tick..."<<std::endl;

                        Tick next_tick;

                        next_tick.symbol[0] = (char)(ABX_buffer[start]);
                        next_tick.symbol[1] = (char)(ABX_buffer[start+1]);
                        next_tick.symbol[2] = (char)(ABX_buffer[start+2]);
                        next_tick.symbol[3] = (char)(ABX_buffer[start+3]);

                        next_tick.buysell_indicator = byteToBuySellIndicator(ABX_buffer, start);
                        next_tick.quantity = byteToQuantity(ABX_buffer, start);
                        next_tick.price = byteToPrice(ABX_buffer, start);
                        next_tick.packet_sequence = byteToPacketSequence(ABX_buffer, start);

                        //Integrity checks:
                        if(tickIntegrityCheck(next_tick))
                        {
                            if(next_tick.packet_sequence!=seq_num)
                            {
                                while(seq_num<next_tick.packet_sequence)
                                {
                                    missed_packets.push(seq_num++);
                                }
                            }
                            tick_vector.push_back(next_tick);
                        }
                        else
                        {
                            //std::cout<<"Data integrity check failed, shutting down..."<<std::endl;
                            log<<"ERROR12: Data integrity check failed, shutting down..."<<std::endl;                
                            cleanupAndClose();
                            exit(0);
                        }
                        
                        seq_num++;
                        ABX_buffer_size-=packet_size_default;
                        start += packet_size_default;
                    }
                }
            }

            void sendSpecificPacketRequests()
            {
                int n = client::missed_packets.top();
                if(!WSAstartup_successful || !connection_successful)
                {
                    return;
                }
                //Preparing payload    
                payload[0] = 2;
                payload[1] = n;

                //Sending request to ABX server
                int send_return = send(ABX_socket, (char*)payload, request_buffer_size, 0);
                if(send_return == SOCKET_ERROR)
                {
                    //std::cout<<"Sending request for packet "<<n<<" failed"<<std::endl;
                    log<<"ERROR13: Sending request for packet "<<n<<" failed"<<std::endl;
                    cleanupAndClose();
                    return;
                }
                //std::cout<<"Sending request for packet "<<n<<" successful"<<std::endl;
                log<<"INFO: Sending request for packet "<<n<<" successful"<<std::endl;

                unsigned char ABX_buffer[received_buffer_size];
                int ABX_buffer_size = recv(ABX_socket, (char*)ABX_buffer, received_buffer_size, 0);

                if(ABX_buffer_size==SOCKET_ERROR || ABX_buffer_size == 0)
                {
                    //std::cout<<"No data received, socket error while reading next tick"<<std::endl;
                    log<<"ERROR14: No data received, socket error while reading next tick"<<std::endl;
                    cleanupAndClose();
                    return;
                }

                //std::cout<<"Parsing received tick..."<<std::endl; 
                log<<"INFO: Parsing received tick..."<<std::endl;

                Tick next_tick;

                next_tick.symbol[0] = (char)(ABX_buffer[0]);
                next_tick.symbol[1] = (char)(ABX_buffer[1]);
                next_tick.symbol[2] = (char)(ABX_buffer[2]);
                next_tick.symbol[3] = (char)(ABX_buffer[3]);

                next_tick.buysell_indicator = byteToBuySellIndicator(ABX_buffer, 0);
                next_tick.quantity = byteToQuantity(ABX_buffer, 0);
                next_tick.price = byteToPrice(ABX_buffer, 0);
                next_tick.packet_sequence = byteToPacketSequence(ABX_buffer, 0);

                //Integrity checks:
                if(tickIntegrityCheck(next_tick))
                {
                    tick_vector.push_back(next_tick);
                    client::missed_packets.pop();
                }
                else
                {
                    //std::cout<<"Data integrity check failed, shutting down..."<<std::endl;
                    log<<"ERROR15: Data integrity check failed, shutting down..."<<std::endl;
                    cleanupAndClose();
                    exit(0);
                }
            }
    }; 

    class Comparator
    {
    public:
        bool operator()(client::Tick T1, client::Tick T2)
        {
            return T1.packet_sequence<T2.packet_sequence;
        }
    }; 

    bool writeJSON()
    {
        std::ofstream fout ("tick_data.json", std::ios::out);
        if(fout.fail())
        {
            return false;
        }        

        fout<<"[\n";

        if(!client::tick_vector.empty())
        {
            fout<<"\t{\n";
            fout<<"\t\t\"symbol\":"<<"\""<<client::tick_vector[0].symbol[0]<<client::tick_vector[0].symbol[1]<<client::tick_vector[0].symbol[2]<<client::tick_vector[0].symbol[3]<<"\",\n";
            fout<<"\t\t\"buysellindicator\":"<<"\""<<client::tick_vector[0].buysell_indicator<<"\",\n";
            fout<<"\t\t\"quantity\":"<<client::tick_vector[0].quantity<<",\n";
            fout<<"\t\t\"price\":"<<client::tick_vector[0].price<<",\n";
            fout<<"\t\t\"packetSequence\":"<<client::tick_vector[0].packet_sequence<<"\n";
            fout<<"\t}";
        }

        for(auto i = 1; i<client::tick_vector.size(); i++)
        {
            fout<<",\n";
            fout<<"\t{\n";
            fout<<"\t\t\"symbol\":"<<"\""<<client::tick_vector[i].symbol[0]<<client::tick_vector[i].symbol[1]<<client::tick_vector[i].symbol[2]<<client::tick_vector[i].symbol[3]<<"\",\n";
            fout<<"\t\t\"buysellindicator\":"<<"\""<<client::tick_vector[i].buysell_indicator<<"\",\n";
            fout<<"\t\t\"quantity\":"<<client::tick_vector[i].quantity<<",\n";
            fout<<"\t\t\"price\":"<<client::tick_vector[i].price<<",\n";
            fout<<"\t\t\"packetSequence\":"<<client::tick_vector[i].packet_sequence<<"\n";
            fout<<"\t}";        
        }

        fout<<"\n]";

        std::ofstream logfile(client::log_file_name, std::ios::app | std::ios::out);
        if(!logfile.fail())
        {
            logfile<<"INFO: JSON Successfully written, shutting down client..."<<std::endl;
            logfile.close();
        }
        else
        {
            std::cout<<"Unable to open log for writing, proceedign without logging..."<<std::endl;
        }

        fout.close();

        return true;
    }   
}

int main()
{
    std::cout<<"Requesting data from ABX server"<<std::endl;
    while(client::tick_vector.empty())
    {
        client::ClientApplication C_a;
        C_a.sendAllPacketsRequest();
    }

    //std::this_thread::sleep_for(std::chrono::milliseconds(5000));

    if(!client::missed_packets.empty())
    {
        std::cout<<"Some packets missing..."<<std::endl;
        while(!client::missed_packets.empty())
        {
            std::cout<<"Requesting missed packet number "<<client::missed_packets.top()<<std::endl;
            client::ClientApplication C_s;      
            C_s.sendSpecificPacketRequests();
        }
    }

    std::sort(client::tick_vector.begin(), client::tick_vector.end(), client::Comparator()); 

    std::cout<<"All packets received"<<std::endl;
    std::cout<<"Writing JSON..."<<std::endl;

    client::writeJSON();

    std::cout<<"All data written, closing..."<<std::endl;

    return 0;
}