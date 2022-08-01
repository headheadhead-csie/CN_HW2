#include "hw2.h"
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#define PORT 8787

using namespace std;

class Server{
public:
    Server(int port = PORT){
        // Inititlaize fd_set used in select()
        FD_ZERO(&mask_read_r);
        FD_ZERO(&mask_read_w);
        FD_ZERO(&mask_write_r);
        FD_ZERO(&mask_write_w);
        max_client_sockfd = -1;

        // Get socket file descriptor
        while((server_sockfd = socket(AF_INET , SOCK_STREAM, 0)) < 0) {
            perror("socket failed\n");
        }

        // Set server address information
        bzero(&server_addr, sizeof(server_addr)); // erase the data
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        server_addr.sin_port = htons(port);
        
        // Bind the server file descriptor to the server address
        while(bind(server_sockfd, (struct sockaddr *)&server_addr , sizeof(server_addr)) < 0) {
            perror("bind failed\n");
        }
            
        // Listen on the server file descriptor
        while(listen(server_sockfd , 3) < 0) {
            perror("listen failed\n");
        }
    }
    ~Server(){
        close(server_sockfd);
    }
    void run(){
        int client_sockfd;
        int read_byte, write_byte;
        bool readable, writable;
        Connection *c;

        read_byte = write_byte = 0;

        if ((client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_addr, (socklen_t*)&client_addr_len)) < 0) {
        } else{
            add_client(client_sockfd);
            int flag = fcntl(server_sockfd, F_GETFL);
            fcntl(server_sockfd, F_SETFL, flag | O_NONBLOCK);
        }
        while(true) {
            // Accept the client and get client file descriptor
            if ((client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_addr, (socklen_t*)&client_addr_len)) < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("accept failed\n");
                } else{
                    fprintf(stderr, "No client\n");
                    errno = 0;
                }
            } else{
                add_client(client_sockfd);
            }

            memcpy(&mask_read_w, &mask_read_r, sizeof(fd_set));
            memcpy(&mask_write_w, &mask_write_r, sizeof(fd_set));
            if (select(max_client_sockfd, &mask_read_w, &mask_write_w, NULL, NULL) < 0) {
                perror("select failed\n");
                continue;
            }
            fprintf(stderr, "select finished\n");

            for(int i = 0; i < (int)clients.size(); i++) {
                c = clients.front();

                // Always break when the sokcet is readable,
                // since we have to check whether the socket
                // is disconnected.
                writable = FD_ISSET(c->sockfd, &mask_write_w);
                readable = FD_ISSET(c->sockfd, &mask_read_w);
                if (writable || readable) {
                    break;
                }
                clients.pop();
            }
            
            fprintf(stderr, "writable:%d, readable:%d\n", writable, readable);
            fprintf(stderr, "content: %s\n", (c->content == text)? "text": "data");
            if (c->action == transmit) {
                write_byte = c->send_and_confirm(readable, writable);
            } else if (c->action == receive) {
                read_byte = c->read_and_confirm(readable, writable);
            }
            fprintf(stderr, "c write_len: %d, write_byte: %d\n", c->write_len, c->write_byte);
            fprintf(stderr, "read_byte: %d, write_byte: %d\n", read_byte, write_byte);
            fprintf(stderr, "read_buffer:%s\n", c->read_buffer);
            if (c->is_confirmed) {
                fprintf(stderr, "is_confirmed\n");
            }

            // The client is disconnected
            if (read_byte < 0 || write_byte < 0) {
                delete_client();
                continue;
            } else {
                c->routine(c);
                clients.pop();
                clients.push(c); 
            }
        }
    }
private:
    // socket related variables
    int server_sockfd;
    struct sockaddr_in server_addr, client_addr;
    int client_addr_len = sizeof(client_addr);

    // Manage client connections with select().
    // The variables with _r prefix records which fds should be noticed.
    // The variables with _w prefix will be passed into select().
    int max_client_sockfd;
    queue<Connection*> clients;
    fd_set mask_read_r, mask_read_w, mask_write_r, mask_write_w;

    static void init_connection(Connection *c){
        c->action = receive;
        c->file_name_len = 0;
        c->is_confirmed = false;
        c->routine = run_none;
        c->set_read_message(text);
    }
    void add_client(int client_sockfd){
        Connection *c = new Connection(client_sockfd);
        snprintf(c->name, NAME_SIZE, "NeVeR_LosEs%d", rand());
        c->action = receive;
        c->content = text;
        c->file_name_len = 0;
        c->identity = server;
        c->id_len = 0;
        c->routine = run_none;
        c->name_len = strlen(c->name)+1;
        c->write_mask = &mask_write_r;
        c->read_mask = &mask_read_r;
        c->set_read_message(text);
        clients.push(c);
        max_client_sockfd = max(client_sockfd+1, max_client_sockfd+1);
        return;
    }
    void delete_client(){
        Connection *c = clients.front();
        clients.pop();
        FD_CLR(c->sockfd, &mask_read_r);
        FD_CLR(c->sockfd, &mask_write_r);
        delete c;
        return;
    }

    /*  The structure of run_${command}:
        prepare_buffer: 
        prepare data and update the variables related
        to buffer, i.e. {read, write}_{byte, len}
      
        update state:
        update the variables related to the state of process,
        i.e. c->action, c->command.
        Sometimes it may set c->{write/read}_{byte/len}, too,
        which can be regarded as initialization.
    */  
    static void run_none(Connection *c){
        fprintf(stderr, "run_none\n");
        // prepare_buffer

        // update the state of c
        if (c->is_confirmed) {
            if (c->action == receive) {
                if (c->id_len == 0) {      // receive client id
                    memcpy(c->client_id , c->read_buffer,  c->read_byte);
                    c->id_len = c->read_byte;
                    c->set_write_message(c->name_len, text, c->name);
                    strcpy(c->directory, "b08902062_");
                    strcat(c->directory, c->client_id);
                    strcat(c->directory, "_client_folder");
                    fprintf(stderr, "directory: %s\n", c->directory);
                    c->action = transmit;
                    fprintf(stderr, "run_none end 1\n");
                    return;
                } else {                   // receive command
                    memcpy(c->command_buffer, c->read_buffer, c->read_byte);
                    c->command_len = c->read_byte;
                    set_routine(c);
                    setup_mission(c);
                    fprintf(stderr, "run_none end 3\n");
                    return;
                }
            }
            if (c->action == transmit) {
                fprintf(stderr, "run_none end 2\n");
                c->set_read_message(text);
                c->action = receive;
                return;
            }
        }
        return;
    }
    static void run_ls(Connection *c){
        fprintf(stderr, "%s\n", __func__);

        // prepare_buffer
        if (c->action == transmit) {
            if (c->write_byte == c->write_len) {
                c->write_byte = c->write_len = 0;
            }
            while ((c->write_len < BUFF_SIZE) &&
                   ((c->dir = readdir(c->dp)) != NULL)) {
                fprintf(stderr, "%s\n", c->write_buffer);
                sprintf(c->write_buffer + c->write_len, "%s\n", c->dir->d_name);
                c->write_len += strlen(c->dir->d_name)+1;
            }
            if (c->dir == NULL) {
                sprintf(c->write_buffer+c->write_len, "%s", c->name);
                c->data_size = 0;
            }
        }
        
        // update the state of c
        if (c->is_confirmed) {
            init_connection(c);
        }
    }
    static void run_put(Connection *c){
    }
    static void run_get(Connection *c){
    }
    static void run_play(Connection *c){
    }
    static int set_routine(Connection *c){
        init_command_token(c);
        fprintf(stderr, "command_token:%s\n", c->command_token);
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
        return 0;
    }
    static void setup_mission(Connection *c){
        fprintf(stderr, "setup_mission\n");

        c->action = transmit;
        c->set_write_message(0, data, NULL);
        get_next_file_name(c);
        fprintf(stderr, "command_token:%s\n", c->command_token);
        if (c->routine == &run_ls) {
           mkdir(c->directory, 0755);
            c->dp = opendir(c->directory);
            run_ls(c);
        } else if (c->routine == &run_get) {
            c->fp = fopen(c->command_token, "r");
        } else if (c->routine == &run_put) {
            c->fp = fopen(c->command_token, "w");
        } else if (c->routine == &run_play) {
            c->cap.open(c->command_token);
        }
        fprintf(stderr, "setup_mission end\n");
    }
};
int main(int argc, char *argv[]){
    int port;
    if (argc != 2) {
        fprintf(stderr, "usage: server ${port}\n");
        return 0;
    }

    srand(time(NULL));
    port = atoi(argv[1]);

    Server server(port);
    server.run();
    return 0;
}
