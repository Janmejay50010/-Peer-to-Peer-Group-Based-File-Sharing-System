#include <bits/stdc++.h>
#include <sys/types.h>  
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <openssl/sha.h>
#include <pthread.h>
using namespace std;


#define queuelimit 30
#define CHUNK_SIZE (512*1024)

struct peer_and_tracker_info{
	string client_ip;
	string client_server_port;
	string tracker_server_port;
};
peer_and_tracker_info port_and_ip_info;

struct chunks_info{
    string filename;
    string group;
    unordered_map<string,string> chunk_hash;
    string bit_vector;
    string filesize;
};

struct get_bit_vector_info_helper{
	string peer_ports;
	string filename;
	string my_port;
	string my_ip;
};

pthread_mutex_t mutex_file = PTHREAD_MUTEX_INITIALIZER;

unordered_map<string,bool> files_completely_downloaded;
unordered_map<string,chunks_info> file_chunks_with_me;
unordered_map<string,unordered_map<string,string>> bit_vector_of;

vector<string> get_tokens(string s, char delimiter = ' '){
	vector <string> tokens; 
    // stringstream class check1 
    stringstream check1(s); 
    string intermediate; 
    // Tokenizing w.r.t. space ' ' 
    while(getline(check1, intermediate, delimiter)) { 
        tokens.push_back(intermediate); 
    } 
    return tokens;	
}

int get_filesize(string filepath){
    int size;
    FILE *fp = fopen(filepath.c_str(), "rb");
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);
    return size;
}

string GetHexRepresentation(const unsigned char *Bytes, size_t Length){
    ostringstream os;
    os.fill('0');
    os << hex;
    for(const unsigned char *ptr = Bytes; ptr < Bytes+Length; ++ptr){
        os << setw(2)<<(unsigned int)*ptr;
    }
    return os.str();
}

void store_hash_of_fileChunks_and_other_updates(string filepath){
	int file_size = get_filesize(filepath);
    char chunk_content[CHUNK_SIZE];
   	unsigned char hash[SHA_DIGEST_LENGTH];

    FILE *fp = fopen(filepath.c_str(), "r");
    if(fp == NULL){
        fclose(fp);
        exit(1);
    }
    int chunk_number = 1;
    size_t size;
    while(!feof(fp)){
        while((size = fread(chunk_content, 1, CHUNK_SIZE, fp)) > 0){
        //cout << filepath << endl;
        const unsigned char* unsigned_const_buffer = reinterpret_cast<const unsigned char *>(chunk_content);
        SHA1(unsigned_const_buffer, CHUNK_SIZE, hash);
        
        string chunk_identifier_string = "C" + to_string(chunk_number);
        file_chunks_with_me[filepath].chunk_hash[chunk_identifier_string] = GetHexRepresentation(hash, SHA_DIGEST_LENGTH);
        memset(chunk_content, 0, sizeof(chunk_content));
		memset(hash ,0 , sizeof(hash));
        chunk_number++;
        }
    }
    file_chunks_with_me[filepath].filename = filepath;
	file_chunks_with_me[filepath].group = "--"; // to indicate that this is my file and does not belong to a groups
    file_chunks_with_me[filepath].bit_vector = string(chunk_number-1, '1');
    file_chunks_with_me[filepath].filesize = to_string(file_size);
	files_completely_downloaded[filepath] = 1;
    fclose(fp);
	return;
}

void* get_bit_vector_of_file_from_all_peers(void *args){
	get_bit_vector_info_helper info = *((get_bit_vector_info_helper *)args);
	string peer_ports = info.peer_ports;
	string filename = info.filename;
	vector<string> peer_port_tokens = get_tokens(peer_ports);
	char response_from_peer[1024];
	for(int i=0; i<peer_port_tokens.size(); i++){
		int peer_port = stoi(peer_port_tokens[i]);
		int clientsoc = socket(PF_INET,SOCK_STREAM, 0);
		if(clientsoc  < 0){
			perror("Client socket creation failed\n");
			exit(1);
		}
		struct sockaddr_in peer_addr;
		memset(&peer_addr, '\0', sizeof(peer_addr));
		peer_addr.sin_family = AF_INET; 
		peer_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
		peer_addr.sin_port = htons(peer_port);
		int re = connect(clientsoc,(struct sockaddr*) &peer_addr,sizeof(peer_addr));
		if(re < 0){
			perror("connect to server failed for client\n");
			exit(1);
		}
		string message = "send_bit_vector " + filename + " " +
        info.my_ip + " " + info.my_port ;
		send(clientsoc, message.c_str(), message.size(), 0);
		recv(clientsoc, (void*)response_from_peer, 1024, 0);
		bit_vector_of[filename][peer_port_tokens[i]] = response_from_peer;
		close(clientsoc);
	}
}

set<pair<int,pair<string,string>>> piecewise_selection_algo(string filename){
	set<pair<int,pair<string,string>>> ps_algo_result;
	auto bit_vectors = bit_vector_of[filename];
    for(auto itr=bit_vectors.begin(); itr!=bit_vectors.end(); itr++){
        string peer_name = itr->first;
        ps_algo_result.insert({0,{peer_name,""}});
    }
    for(int i=0; i<bit_vectors.begin()->second.size() ; i++){
        if(file_chunks_with_me[filename].bit_vector[i] == '0'){
			for(auto itr=ps_algo_result.begin(); itr!=ps_algo_result.end(); itr++){
				string peer_name = (itr->second).first;
				if(bit_vectors[peer_name][i] == '1'){
					int count = itr->first;
					string chunk_to_add = "C" + to_string(i+1);                
					string curr_chunk_list = (itr->second).second;
					if(curr_chunk_list == ""){
						curr_chunk_list = chunk_to_add;
					}
					else{
						curr_chunk_list += ("," + chunk_to_add);
					}
					ps_algo_result.erase(itr);
					ps_algo_result.insert({count+1,{peer_name,curr_chunk_list}});
					break;
				}
			}
		}
    }
    return ps_algo_result;
}

void* write_chunks_in_file(void *args){
	string *argument_ptr = static_cast<std::string*>(args);
	string argument = *argument_ptr; 
	delete argument_ptr;

    vector<string> input = get_tokens(argument, ',');
    string filename = input[input.size()-1];
    int peer_server_port = stoi(input[0]);
    char chunk_content[CHUNK_SIZE];

    int clientsocket = socket(PF_INET,SOCK_STREAM, 0);
    if(clientsocket < 0){
        perror("Client socket creation failed\n");
        exit(1);
    }
    struct sockaddr_in serv_addr;
    memset(&serv_addr, '\0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET; 
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
    serv_addr.sin_port = htons(peer_server_port);
    int return_value = connect(clientsocket,(struct sockaddr*) &serv_addr,sizeof(serv_addr));
    if(return_value < 0){
        perror("connect to server failed for client\n");
        exit(1);
    }
    FILE *fp = fopen(filename.c_str(), "wb");
    for(int i=1; i<input.size()-1; i++){
        int chunk_number = stoi(input[i].substr(1));
        string command_string = string("send_chunk") + " " + input[i] + " " + filename;
        send(clientsocket, command_string.c_str(), command_string.size(), 0);
        int bytes_received = 0;
		
		pthread_mutex_lock(&mutex_file);
		while(bytes_received < CHUNK_SIZE){
			int received = recv(clientsocket, (void*)chunk_content, CHUNK_SIZE, 0);//the chunk info is received
			int pointer_position = fseek(fp, (chunk_number-1)*CHUNK_SIZE + bytes_received, SEEK_SET);
			int bytes_wrote = fwrite(chunk_content, sizeof(char), received, fp);
			bytes_received += received;
		}
		fflush(fp);
		pthread_mutex_unlock(&mutex_file);
		
		file_chunks_with_me[filename].bit_vector[chunk_number-1] = '1';
		memset(chunk_content, 0, CHUNK_SIZE);
    }
}

void* client_process(void* args){
	char response_from_tracker[1024];
	int clientsock = socket(PF_INET,SOCK_STREAM, 0);
	if(clientsock  < 0){
		perror("Client_socket creation failed\n");
		exit(1);
	}
	struct sockaddr_in serv_addr;
	memset(&serv_addr, '\0', sizeof(serv_addr));
	serv_addr.sin_family = AF_INET; 
	serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
	serv_addr.sin_port = htons(stoi(port_and_ip_info.tracker_server_port));//----------------------------------connecting to tracker; tracker ip and port
	
	if(connect(clientsock,(struct sockaddr*) &serv_addr,sizeof(serv_addr)) < 0){
    	perror("connect to tracker failed for client\n");
    	exit(1);
    }
    cout << "Connected to tracker" << "\n";
	
	string command_string;
	cout<< "Enter the command" << "\n";
	while(1){
		getline(cin, command_string);
		command_string = command_string + " 127.0.0.1" + " " + port_and_ip_info.client_server_port;
		vector<string> cmd_tokens = get_tokens(command_string);

		if(cmd_tokens[0] == "create_user"){
			send(clientsock, command_string.c_str(), command_string.size(), 0);
			recv(clientsock, (void*)response_from_tracker, 1024, 0);
			cout << response_from_tracker << "\n"; 
		}
		if(cmd_tokens[0] == "login"){
			send(clientsock, command_string.c_str(), command_string.size(), 0);
			recv(clientsock, (void*)response_from_tracker, 1024, 0);
			cout << response_from_tracker << "\n"; 
		}
		if(cmd_tokens[0] == "create_group"){
			send(clientsock, command_string.c_str(), command_string.size(), 0);
			recv(clientsock, (void*)response_from_tracker, 1024, 0);
			cout << response_from_tracker << "\n"; 
		}
		if(cmd_tokens[0] == "join_group"){
			send(clientsock, command_string.c_str(), command_string.size(), 0);
			recv(clientsock, (void*)response_from_tracker, 1024, 0);
			cout << response_from_tracker << "\n";
		}
		if(cmd_tokens[0] == "list_requests"){
			send(clientsock, command_string.c_str(), command_string.size(), 0);
			recv(clientsock, (void*)response_from_tracker, 1024, 0);
			cout << "The pending requests are " << "\n";
			cout << response_from_tracker << "\n";
		}
		if(cmd_tokens[0] == "accept_request"){
			send(clientsock, command_string.c_str(), command_string.size(), 0);
			recv(clientsock, (void*)response_from_tracker, 1024, 0);
			cout << response_from_tracker << "\n";
		}
		if(cmd_tokens[0] == "leave_group"){
			send(clientsock, command_string.c_str(), command_string.size(), 0);
			recv(clientsock, (void*)response_from_tracker, 1024, 0);
			cout << response_from_tracker << "\n"; 
		}
		if(cmd_tokens[0] == "list_groups"){
			send(clientsock, command_string.c_str(), command_string.size(), 0);
			recv(clientsock, (void*)response_from_tracker, 1024, 0);
			cout << response_from_tracker << "\n"; 
		}
		if(cmd_tokens[0] == "upload_file"){
			string filename = cmd_tokens[1];
			string group = cmd_tokens[2];
			store_hash_of_fileChunks_and_other_updates(filename);
			string command_and_file_details = cmd_tokens[0] + " " + cmd_tokens[1] + " " + cmd_tokens[2] + " " + 
			file_chunks_with_me[filename].filesize + " " + to_string(file_chunks_with_me[filename].chunk_hash.size()) 
			+ " " + cmd_tokens[cmd_tokens.size()-2] + " " + cmd_tokens[cmd_tokens.size()-1];
			
			send(clientsock, command_and_file_details.c_str(), command_and_file_details.size(), 0);
			recv(clientsock, (void*)response_from_tracker, 1024, 0);
			cout << response_from_tracker << "\n"; 
		}
		if(cmd_tokens[0] == "list_files"){
			string group = cmd_tokens[1];
			send(clientsock, command_string.c_str(), command_string.size(), 0);
			recv(clientsock, (void*)response_from_tracker, 1024, 0);
			if(response_from_tracker != ""){
				cout << "Getting the the list of shareable files are " << "\n";		
				cout << response_from_tracker << "\n"; 
			}
			else{
				cout << "There are no shareable files in group" << "\n";
			}
		}
		if(cmd_tokens[0] == "download_file"){
			string group = cmd_tokens[1];
			string filename = cmd_tokens[2];
			string destination_path = cmd_tokens[3];
			send(clientsock, command_string.c_str(), command_string.size(), 0);
			recv(clientsock, (void*)response_from_tracker, 1024, 0);
			vector<string> response_tokens = get_tokens(response_from_tracker);
			
			int filesize = stoi(response_tokens[0]);
			int num_peers = stoi(response_tokens[response_tokens.size()-1]);
			
			if(filesize != -1 && num_peers !=0){
				get_bit_vector_info_helper instance1;
				pthread_t thread;
				instance1.filename = filename;
				instance1.peer_ports = "";
				instance1.my_port = port_and_ip_info.client_server_port;
                instance1.my_ip = "127.0.0.1";
				for(int i=1; i<response_tokens.size()-1; i++){
					instance1.peer_ports += (response_tokens[i] + " ");
				}
				pthread_create(&thread, NULL, get_bit_vector_of_file_from_all_peers, (void*)&instance1);
				pthread_join(thread, NULL);
				
				file_chunks_with_me[filename].filename = filename;
				file_chunks_with_me[filename].group = group;
				file_chunks_with_me[filename].filesize = to_string(filesize);
				int number_of_chunks_in_file = ceil((float)filesize/CHUNK_SIZE);
				file_chunks_with_me[filename].bit_vector = string(number_of_chunks_in_file, '0');
				
				set<pair<int,pair<string,string>>> ps_algo_result = piecewise_selection_algo(filename);
				if(ps_algo_result.size() == 0){
					string message = "No peer has complete file, download can't proceed";
					continue;
				}
				cout << "Download of file " << filename << " has started" << "\n";
				
				pthread_t thread_to_write_chunks[5];
            	int thread_count = 0;
            	for(auto itr=ps_algo_result.begin(); itr!=ps_algo_result.end(); itr++){
                	if(itr->first != 0){
                    	string argument = itr->second.first + "," + itr->second.second + "," + filename;
                    	void *argument_ptr = static_cast<void*>(new string(argument));
						pthread_create(&thread_to_write_chunks[thread_count++], NULL, write_chunks_in_file, argument_ptr);
                	}
				}
				for(int i=0; i<thread_count; i++){
            		pthread_join(thread_to_write_chunks[i], NULL);
            	}
            }
        	else{
				if(filesize == -1){
					cout << "The file does not exist in the group" << "\n";
				}
				else if(num_peers == 0){
					cout << "None of the peers who have this file are online" << "\n";
				}
			}
		}
		if(cmd_tokens[0] == "logout"){
			send(clientsock, command_string.c_str(), command_string.size(), 0);
			recv(clientsock, (void*)response_from_tracker, 1024, 0);
			cout << response_from_tracker << "\n";
		}
		if(cmd_tokens[0] == "stop_share"){
			send(clientsock, command_string.c_str(), command_string.size(), 0);
			recv(clientsock, (void*)response_from_tracker, 1024, 0);
			cout << response_from_tracker << "\n";
		}
		if(cmd_tokens[0] == "show_downloads"){
			string status = "C";
			for(auto itr = file_chunks_with_me.begin(); itr != file_chunks_with_me.end(); itr++){
				string filename = itr->first;
				auto file_info = file_chunks_with_me[filename];
				if(file_info.group != "--" && file_info.filename != ""){
					for(int i=0; i<file_info.bit_vector.length(); i++){
						if(file_info.bit_vector[i] == '0'){
							status = "D";
							break;
						}
					}
					cout << status << " " << file_info.group << " " << file_info.filename << "\n";
				}
			}
		}
		memset(response_from_tracker,0, sizeof(response_from_tracker));
		fflush(stdin);
	}
	fflush(stdin);
	pthread_exit(NULL);
}

void* server_process(void* args){
	char response_from_peer[1024];
	char file_chunk_content[CHUNK_SIZE];
	int newsocket = *((int *)args);
	cout << "Connected to " << newsocket << " on server side" << endl;
	while(1){
		recv(newsocket, (void*)response_from_peer, 1024, 0);
		vector<string> response_tokens = get_tokens(response_from_peer,' ');
        memset(response_from_peer, 0, sizeof(response_from_peer));
		string client_ip = response_tokens[response_tokens.size()-2];
        string client_port = response_tokens[response_tokens.size()-1];
		if(response_tokens[0] == "send_chunk"){
			string chunk = response_tokens[1];
			string filename = response_tokens[2];
			int chunk_number = stoi(chunk.substr(1));
			
            //Read from file and send it peer_client
			cout << "Sending chunk " << "C" << chunk_number << " of file " << filename << endl;
            FILE *fp = fopen(filename.c_str(), "rb");;
			fseek(fp, (chunk_number-1)*CHUNK_SIZE, SEEK_SET);
			int bytes_read = fread(file_chunk_content, sizeof(char), CHUNK_SIZE, fp);
			int sent = 0;
            int bytes_sent = 0;
            while(sent < bytes_read){
                bytes_sent = send(newsocket, file_chunk_content, CHUNK_SIZE, 0);
                sent += bytes_sent;
            }
            memset(file_chunk_content, 0, CHUNK_SIZE);
			fclose(fp);
		}
		if(response_tokens[0] == "send_bit_vector"){
			string filename = response_tokens[1];
			string bit_vector  = file_chunks_with_me[filename].bit_vector;
			send(newsocket, bit_vector.c_str(), bit_vector.size(), 0);
			cout << "sent bit vector " << bit_vector << " to " << client_port << "\n";
			break;
		}
	}
	close(newsocket);
	fflush(stdout);
}

int main(int argc, char const *argv[]){
    
	//cout << "Command_line arguments are " << argv[1] << " and " << argv[2] << endl;
	vector<string> ip_and_port_number = get_tokens(argv[1], ':');
	string client_ip = ip_and_port_number[0];
	string client_server_port = ip_and_port_number[1];
    
	string tracker_info = argv[2];
    string tracker_ip = tracker_info.substr(0, tracker_info.find(":"));
    string tracker_server_port = tracker_info.substr(tracker_info.find(":")+1);

    cout << "My IP adress is: " << client_ip << " , my server_thread is listening to port_no: " << client_server_port << "\n";
    cout << "tracker IP adress is: " << tracker_ip << " , tracker server_thread is listening to port no: " << tracker_server_port << "\n";
    
	// GLobal struct that will hold ip and port of client and tracker
	port_and_ip_info.client_ip = client_ip;
	port_and_ip_info.client_server_port = client_server_port;
	port_and_ip_info.tracker_server_port = tracker_server_port;

	pthread_t client_thread;
	if(pthread_create(&client_thread, NULL, client_process, NULL) != 0){
		cout<<"Failed to create client thread\n";
	}
	/*------------------------------------------Server------------------------------------*/
	pthread_t server_thread;
	struct sockaddr_in serveraddrss;
	struct sockaddr_in newaddrs;
	int newsock;
	
	int socket_fd = socket(PF_INET, SOCK_STREAM, 0);
	if(socket_fd < 0){
		perror("socket failed\n");
		exit(1);
	}
	cout << "Socket created" << "\n";
	memset(&serveraddrss, '\0', sizeof(serveraddrss));
	serveraddrss.sin_family = AF_INET; 
	serveraddrss.sin_addr.s_addr = INADDR_ANY;
	string server_port = client_server_port;
	serveraddrss.sin_port = htons(stoi(client_server_port));
	int return_value = bind(socket_fd,(struct sockaddr*) &serveraddrss, sizeof(serveraddrss)); 
    if(return_value < 0){
    	cout << "Error in binding" << "\n";
    	exit(1);
    }
    cout << "Bind Successful" << "\n";
    int status = listen(socket_fd,queuelimit);
    if(status < 0){
    	cout << "Could not listen..." << "\n";
    	exit(1);
    }
	socklen_t addrlen= sizeof(newaddrs);
    while(newsock = accept(socket_fd,(struct sockaddr*)&newaddrs, (socklen_t*) &addrlen)){
    	if(pthread_create(&server_thread, NULL, server_process, &newsock)!=0){
	    	cout << "Failed to create server request service thread" << "\n";
	    }
	}
	pthread_join(client_thread, NULL);
	pthread_exit(NULL);
	return 0;
}