#include <bits/stdc++.h>
#include <sys/types.h>  
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <openssl/sha.h>
#include <pthread.h>
using namespace std;

#define queuelimit 10

struct file_info{
    string filesize;
    string filename;
    vector<string> user_has;
    string num_chunks;
    //unordered_map accessible
};

struct tracker_info{
	string tracker_ip;
	string tracker_server_port;
};
tracker_info port_and_ip_info;

unordered_map<string,bool> registered;
unordered_map<string,string> username;
unordered_map<string,string> password;
unordered_map<string,bool> online;

unordered_map<string,unordered_map<string,bool>> user_to_group;
unordered_map<string,unordered_map<string,bool>> group_to_user;
unordered_map<string,string> group_owner;
unordered_map<string,unordered_map<string,bool>> files_in_group;
set<string> groups;
unordered_map<string,unordered_map<string,string>> pending_join_requests;
unordered_map<string,unordered_map<string,string>> accepeted_join_requests;

unordered_map<string,file_info> file_details;

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

void* tracker_server(void *arg){
    int newsock = *((int *)arg);
    char command_string[1024]; //This will store the commands sent by client/peer
    while(1){
        memset(command_string, 0, sizeof(command_string));
        recv(newsock, (void*)command_string, 1024, 0);
        vector<string> cmd_tokens = get_tokens(command_string);
        memset(command_string, 0, sizeof(command_string));
		fflush(stdout);

        string peer_ip = cmd_tokens[cmd_tokens.size()-2];
        string peer_port = cmd_tokens[cmd_tokens.size()-1];

        if(cmd_tokens[0] == "create_user"){
            if(registered[peer_port] == 1){
                string message = "Username already exists";
                send(newsock, message.c_str(), message.size(), 0);
            }
            else{
                registered[peer_port] = 1;
                username[peer_port] = cmd_tokens[1];
                password[peer_port] = cmd_tokens[2];
                string message = "Account created";
                send(newsock, message.c_str(), message.size(), 0);
            }         
        }
        if(cmd_tokens[0] == "login"){
            if(username[peer_port] == cmd_tokens[1] && password[peer_port] == cmd_tokens[2]){
                string message = "You are logged in\n";
                send(newsock, message.c_str(), message.size(), 0);
                online[peer_port] = 1;
            }
            else{
                string message = "Username or password is incorrect";
                send(newsock, message.c_str(), message.size(), 0);
            }         
        }
        if(cmd_tokens[0] == "create_group"){
            string group = cmd_tokens[1];
            if(online[peer_port] && groups.find(group) == groups.end()){
                groups.insert(group);
                group_owner[group] = peer_port;
                user_to_group[peer_port][group] = 1;
                group_to_user[group][peer_port] = 1;
                string message = "group created";
                send(newsock, message.c_str(), message.size(), 0);
            }
            else{
                if(!online[peer_port]){
                    string message = "register and login first";
                    send(newsock, message.c_str(), message.size(), 0);
                }
                if(groups.find(group) != groups.end()){
                    string message = "group id already exists,choose a different id";
                    send(newsock, message.c_str(), message.size(), 0);
                }
            }
        }
        if(cmd_tokens[0] == "list_groups"){
            if(online[peer_port]){
                string message = "";
                for(auto itr = groups.begin(); itr != groups.end(); itr++){
                    message += (*itr + " ");
                }
                send(newsock, message.c_str(), message.size(), 0);
            }
            else{
                string message = "You are not logged in";
                send(newsock, message.c_str(), message.size(), 0);
            }
        }
        if(cmd_tokens[0] == "join_group"){
            string group = cmd_tokens[1];
            auto itr = groups.find(group);
            if(online[peer_port] && itr!=groups.end()){
                pending_join_requests[group][peer_port] = "Request pending";
                string message = "Request pending to be accepted";
                send(newsock, message.c_str(), message.size(), 0);
            }
            else{
                if(!online[peer_port]){
                    string message = "You are not logged in";
                    send(newsock, message.c_str(), message.size(), 0);
                }
                else if(itr!=groups.end()){
                    string message = "The group does not exist";
                    send(newsock, message.c_str(), message.size(), 0);
                }
            }
        }
        if(cmd_tokens[0] == "list_requests"){
            string group = cmd_tokens[1];
            auto itr = groups.find(group);
            if(online[peer_port] && itr!=groups.end() && group_owner[group] == peer_port){
                    string message = "";
                    for(auto itr=pending_join_requests[group].begin();itr!=pending_join_requests[group].end(); itr++){
                        message += (itr->first + " ");
                    }
                    if(message == ""){
                        message = "No Pending requests";
                    }
                    send(newsock, message.c_str(), message.size(), 0);
            }
            else{
                if(!online[peer_port]){
                    string message = "You are not logged in";
                    send(newsock, message.c_str(), message.size(), 0);
                }
                else if(group_owner[group] != peer_port){
                    string message = "You are not owner of the group";
                    send(newsock, message.c_str(), message.size(), 0);
                }
            }
        }
        if(cmd_tokens[0] == "accept_request"){
            string group = cmd_tokens[1];
            string client_port = cmd_tokens[2];
            auto itr = groups.find(group);
            if(online[peer_port] && itr!=groups.end() && group_owner[group] == peer_port){
                group_to_user[group][client_port] = 1;
                user_to_group[client_port][group] = 1;
                if(pending_join_requests[group].find(client_port) != pending_join_requests[group].end()){
                    accepeted_join_requests[group][client_port] = "Request Accepted";
                    pending_join_requests[group].erase(client_port);
                }
                string message = "The " + client_port + " is part of the group";
                send(newsock, message.c_str(), message.size(), 0);
            }
            else{
                if(!online[peer_port]){
                    string message = "You are not logged in";
                    send(newsock, message.c_str(), message.size(), 0);
                }
                else if(group_owner[group] != peer_port){
                    string message = "You are not owner of the group";
                    send(newsock, message.c_str(), message.size(), 0);
                }
            }
        }
        if(cmd_tokens[0] == "leave_group"){
            string group = cmd_tokens[1];
            if(online[peer_port]){
                user_to_group[peer_port][group] = 0;
                group_to_user[group][peer_port] = 0;
                auto itr = groups.find(group);
                if(itr != groups.end()){
                    groups.erase(itr);
                }
                if(group_owner[group] == peer_port){
                    group_owner.erase(group);
                    group_to_user.erase(group);
                }
                string message = "You have left the group";
                send(newsock, message.c_str(), message.size(), 0);
            }
            else{
                string message = "You are not logged in";
                send(newsock, message.c_str(), message.size(), 0);
            }
        }
        if(cmd_tokens[0] == "upload_file"){
            string filename = cmd_tokens[1];
		    string group = cmd_tokens[2];
            if(online[peer_port] && group_to_user[group][peer_port] == 1){
                files_in_group[group][filename] = 1;
                file_details[filename].filesize = cmd_tokens[3];
                file_details[filename].num_chunks = cmd_tokens[4];
                file_details[filename].user_has.push_back(peer_port);
                string message = "The file is shareable in the group now";
                send(newsock, message.c_str(), message.size(), 0);
            }
            else{
                if(online[peer_port] == 0){
                    string message = "You are not logged in";
                    send(newsock, message.c_str(), message.size(), 0);
                    continue;
                }   
                if(group_to_user[group][peer_port] == 0){
                    string message = "You are not part of the group";
                    send(newsock, message.c_str(), message.size(), 0);
                }    
            } 
        }
        if(cmd_tokens[0] == "list_files"){
            string group = cmd_tokens[1];
            if(group_to_user[group][peer_port] && online[peer_port]){
                string message = "";
                string group = cmd_tokens[1];
                for(auto itr=files_in_group[group].begin(); itr!=files_in_group[group].end();itr++){
                    if(itr->second == 1){
                    message += (itr->first + " ");
                    }
                }
                if(message == ""){
                    message = "There are no files shared in this group";
                }
                send(newsock, message.c_str(), message.size(), 0);
            }
            else{
                string message;
                if(!online[peer_port]){
                    message = "You are not logged in";
                }
                else if(!group_to_user[group][peer_port]){
                    message = "You are not part of this group";
                }
                send(newsock, message.c_str(), message.size(), 0);
            }
        }
        if(cmd_tokens[0] == "download_file"){
            string group = cmd_tokens[1];
			string filename = cmd_tokens[2];
            if(files_in_group[group][filename] == 1){ //1
                string message = file_details[filename].filesize + " ";
                int num_peers = 0;
                for(auto peer:file_details[filename].user_has){
                    if(group_to_user[group][peer] && online[peer]){
                        message += (peer + " ");
                        num_peers++;
                    }
                }
                //Add the person who is donwloading the file to list of people who have this file
                file_details[filename].user_has.push_back(peer_port); 
                message += (to_string(num_peers) + " ");
                if(num_peers == 0){
                    file_details[filename].user_has.pop_back();
                }
                send(newsock, message.c_str(), message.size(), 0);
            }
            else{
                string message = "-1 0";
                send(newsock, message.c_str(), message.size(), 0);
            }
        }
        if(cmd_tokens[0] == "stop_share"){
            if(online[peer_port]){
                // remove from file_details, this is due to faulty implementation
                string group = cmd_tokens[1];
                string filename = cmd_tokens[2];
                auto file_info = file_details[filename];
                auto itr_vec = find(file_info.user_has.begin(), file_info.user_has.end(), peer_port);
                if(itr_vec != file_info.user_has.end()){
                   file_info.user_has.erase(itr_vec);
                   string message = "File is not shared in the group by you";
                   send(newsock, message.c_str(), message.size(), 0);
                }
                else{
                    string message = "You were not sharing this file in the group";
                    send(newsock, message.c_str(), message.size(), 0);
                }
            }
            else{
                string message = "You are not logged in";
                send(newsock, message.c_str(), message.size(), 0);
            }
        }
        if(cmd_tokens[0] == "logout"){
           online[peer_port] = 0;
        //    for(auto itr = file_details.begin(); itr!= file_details.end(); itr++){
        //        string filename = itr->first;
        //        auto file_info = file_details[filename];
        //        auto itr_vec = find(file_info.user_has.begin(), file_info.user_has.end(), peer_port);
        //        if(itr_vec != file_info.user_has.end()){
        //            file_info.user_has.erase(itr_vec);
        //        }
        //    }
           string message = "You are logged out";
           send(newsock, message.c_str(), message.size(), 0);
        }
    }
	fflush(stdout);
	pthread_exit(NULL);
}

int main(int argc, char* argv[]){
    char *filename = argv[1];
    ifstream trackerfile;
    trackerfile.open(filename);
    string tracker_ip;
    string tracker_server_port;
    getline(trackerfile, tracker_ip);
    getline(trackerfile, tracker_server_port);

    port_and_ip_info.tracker_ip = tracker_ip;
    port_and_ip_info.tracker_server_port = tracker_server_port;

    cout << "IP of tracker is :"<<  port_and_ip_info.tracker_ip << "\n";
    cout << "Port of tracker is :"<<  port_and_ip_info.tracker_server_port << "\n";
	
    struct sockaddr_in serveraddrss;
	struct sockaddr_in newaddrs;
	socklen_t addrlen;
	int newsock;
	int socketfd = socket(PF_INET, SOCK_STREAM, 0);
	if(socketfd < 0){	//tcp -sock_stream  af_inet - ipv4
	    perror("socket failed\n");
		exit(1);
	}
    /************************************************************BIND*************************************************/
	memset(&serveraddrss, '\0', sizeof(serveraddrss));
	serveraddrss.sin_family = AF_INET;
    string trackerip = "127.0.0.1";
    string trackerport =  port_and_ip_info.tracker_server_port;
    serveraddrss.sin_addr.s_addr = INADDR_ANY;  //inet_addr(trackerip1.c_str()); 
    
    int tracker_port = stoi(trackerport);
    serveraddrss.sin_port = htons(tracker_port );
    cout << "Port of tracker: "<< tracker_port << endl;
    int retu = bind(socketfd,(struct sockaddr*) &serveraddrss, sizeof(serveraddrss)); 
    if(retu < 0){
    	cout << "Error in binding\n";
    	exit(1);
    }
    /****************************************************LISTEN************************************************************/
    int status = listen(socketfd,queuelimit);
    if(status < 0){
    	cout << "error in listen\n";
    	exit(1);
    }
    /********************************accept connection****************************************************************************/
    pthread_t tid[10];
    int i=0;
    addrlen= sizeof(newaddrs);
    while(newsock = accept(socketfd,(struct sockaddr*)&newaddrs, (socklen_t*)&addrlen)){ 	
	    //accept is a blckng call
	    if(pthread_create(&tid[i++], NULL, tracker_server, &newsock)!= 0 ){
	    	cout << "failed to create thread\n";
	    } 
	}    
    close(socketfd);
    pthread_exit(NULL);
	return 0;
}