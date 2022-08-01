#include "hw2.h"

#define PORT 8787
#define ERR_EXIT(a){ perror(a); exit(1); }
class Client{
public:
    Client(int port = 8787, char* server_ip = NULL, int client_id = 1){
        id = client_id;

        // Get socket file descriptor
        while ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("socket failed\n");
        }
        c = new Connection(sockfd);

        // Set server address
        bzero(&addr,sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr((server_ip == NULL)? "127.0.0.1": server_ip);
        addr.sin_port = htons(port);
        while (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("connect failed\n");
        }

        return;
    }
    ~Client(){
        close(sockfd);
    }
    void run(){
        init_connection(c);

        // First, we have to send ID to the server
        // The server shouldn't crash, there is no need to handle
        // such cases.
        sprintf(c->client_id, "%d", id);
        c->set_write_message(strlen(c->client_id)+1, text, c->client_id);
        fprintf(stderr, "1\n");
        while (!c->is_confirmed) {
            c->send_and_confirm();
        }
        
        fprintf(stderr, "2\n");
        c->set_read_message(text);
        while(!c->is_confirmed) {
            fprintf(stderr, "read_byte: %d\n", c->read_byte);
            c->read_and_confirm();
        }
        fprintf(stderr, "name: %s\n", c->read_buffer);
        strcpy(c->name, c->read_buffer); 

        while (true) {
            run_none(c);
            if (set_routine(c) < 0) {
                continue;
            }
            c->routine(c);
        }
    }
private:
    int sockfd, id;
    struct sockaddr_in addr;
    Connection *c;
    static void init_connection(Connection *c){
        c->routine = run_none;
        c->action = transmit;
        c->identity = client;
        c->is_confirmed = false;
        c->write_len = c->confirm_len = c->read_len = c->read_byte = c->write_byte = 0;
    }
    static void run_none(Connection *c){
        fprintf(stderr, "run_none\n");
        scanf("%s", c->command_buffer);
        fprintf(stderr, "run_none end\n");
    }
    static void run_ls(Connection *c){
        fprintf(stderr, "run_ls\n");
        int read_byte;

        c->set_read_message(data);
        while(!c->is_confirmed) {
            read_byte = c->read_and_confirm();
            write(1, c->read_buffer, read_byte);
        }
        fprintf(stderr, "run_ls end\n");
        return;
    }
    static void run_put(Connection *c){
        return;
    }
    static void run_get(Connection *c){
        return;
    }
    static void run_play(Connection *c){
        return;
    }
    int set_routine(Connection *c){
        c->command_len = strlen(c->command_buffer)+1;
        c->set_write_message(c->command_len, text, c->command_buffer);
        init_command_token(c);
        if (strncmp(c->command_token, "ls", 2) == 0) {
            c->routine = &run_ls;
        } else if (strncmp(c->command_token, "put", 3) == 0) {
            c->routine = &run_put;
        } else if (strncmp(c->command_token, "get", 3) == 0) {
            c->routine = &run_get;
        } else if (strncmp(c->command_token, "play", 4) == 0) {
            c->routine = &run_play;
        } else {
            fprintf(stderr, "The command is wrong\n");
            return -1;
        }
        c->send_and_confirm();
        return 0;
    }
};
int main(int argc , char *argv[]){
    int client_id, port;
    char *server_ip;
    if (argc != 3) {
        fprintf(stderr, "usage: server $client_id ${ip}:${port}\n");
        return 0;
    }

    client_id = atoi(argv[1]);
    server_ip = strtok(argv[2], ":");
    port = atoi(strtok(NULL, ":"));


    Client client(port, server_ip, client_id);
    client.run();

    return 0;
}

