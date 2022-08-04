#include "hw2.h"
#include <fcntl.h>
#include <opencv2/videoio.hpp>
#include <opencv2/videoio/videoio_c.h>
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
        max_sockfd = -1;

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

        mkdir("b08902062_server_folder", 0755);
        chdir("b08902062_server_folder");

        read_byte = write_byte = 0;
        FD_SET(server_sockfd, &mask_read_r);
        max_sockfd = server_sockfd;
        while(true) {
            read_byte = write_byte = 0;

            mask_read_w = mask_read_r;
            mask_write_w = mask_write_r;
            if (select(max_sockfd+1, &mask_read_w, &mask_write_w, NULL, NULL) < 0) {
                perror("select failed\n");
                continue;
            }
            if (FD_ISSET(server_sockfd, &mask_read_w)) {
                // Accept the client and get client file descriptor
                if ((client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_addr,
                    (socklen_t*)&client_addr_len)) < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        perror("accept failed\n");
                    } else{
                        errno = 0;
                    }
                } else{
                    printf("new connection: %d\n", client_sockfd);
                    add_client(client_sockfd);
                }
                continue;
            }

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
                clients.push(c);
            }
            if (!(writable || readable)) {
                continue;
            }
            if (c->action == transmit) {
                write_byte = c->send_and_confirm(readable, writable, c->need_confirm);
            } else if (c->action == receive) {
                read_byte = c->read_and_confirm(readable, writable, c->need_confirm);
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
    int max_sockfd;
    queue<Connection*> clients;
    fd_set mask_read_r, mask_read_w, mask_write_r, mask_write_w;

    static void init_connection(Connection *c){
        c->action = receive;
        c->is_confirmed = false;
        c->routine = run_none;
        c->need_confirm = false;
        c->output_len = c->output_ptr = c->buffer_len = c->file_name_len = 0;
        c->dir = NULL;
        c->dp = NULL;
        c->fp = NULL;
        c->cap.release();
        c->set_read_message(text);
    }
    void add_client(int client_sockfd){
        Connection *c = new Connection(client_sockfd);
        int flag = fcntl(server_sockfd, F_GETFL);
        fcntl(client_sockfd, F_SETFL, flag | O_NONBLOCK);
        snprintf(c->name, NAME_SIZE, "NeVeR_LosEs%d", rand());
        c->action = transmit;
        c->content = text;
        c->file_name_len = 0;
        c->identity = server;
        c->routine = run_none;
        c->name_len = strlen(c->name)+1;
        c->need_confirm = false;
        c->write_mask = &mask_write_r;
        c->read_mask = &mask_read_r;
        c->set_write_message(c->name_len, text, c->name);
        clients.push(c);
        max_sockfd = max(client_sockfd, max_sockfd);
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
        // prepare_buffer

        // update the state of c
        if (c->is_confirmed) {
            if (c->action == receive) {
                memcpy(c->command_buffer, c->read_buffer, c->read_byte);
                c->command_len = c->read_byte;
                set_routine(c);
                setup_mission(c);
            } else if (c->action == transmit) {
                c->set_read_message(text);
                c->action = receive;
            }
        }
        return;
    }
    static void run_ls(Connection *c){
        // prepare_buffer
        if (c->action == transmit) {
            if (c->write_byte == c->write_len) {
                c->write_len = c->write_byte = 0;
            }
            while ((c->write_len < BUFF_SIZE) &&
                   ((c->dir = readdir(c->dp)) != NULL)) {
                sprintf(c->write_buffer + c->write_len, "%s\n", c->dir->d_name);
                c->write_len += strlen(c->dir->d_name)+1;
            }
            if (c->dir == NULL && c->data_size != 0) {
                sprintf(c->write_buffer+c->write_len, "%s", c->name);
                c->write_len += c->name_len;
                c->data_size = 0;
            }
        }
        
        if (!c->is_confirmed) {
            return;
        }
        // update the state of c
        init_connection(c);
        return;
    }
    static void run_get(Connection *c){
        char tmp_buf[1] = {'\0'};
        // prepare_buffer
        if (c->fp == NULL && c->file_name_len == 0 && c->need_confirm) {
            get_next_file_name(c);
            if (c->command_token != NULL) {
                strcpy(c->file_name, c->command_token);
                c->fp = fopen(c->file_name, "r"); 
            } else {
                c->is_confirmed = true;
            }
        }
        if (c->command_token != NULL) {
            if (c->content == text && c->file_name_len == 0) { // transfer file name
                c->file_name_len = strlen(c->command_token) + 1;
                if (c->fp != NULL) {
                    c->set_write_message(c->file_name_len, text, c->command_token);
                } else { // no such file
                    c->set_write_message(1, text, tmp_buf);
                }
            } else if (c->content == data) { // transfer file content
                if (c->write_byte == c->write_len) {
                    c->write_byte = 0;
                    c->write_len = fread(c->write_buffer, sizeof(char), BUFF_SIZE, c->fp);
                }
                if (feof(c->fp) && c->data_size != 0) {
                    sprintf(c->write_buffer+c->write_len, "%s", c->name);
                    c->write_len += c->name_len;
                    c->data_size = 0;
                }
            }
        }

        if (!c->is_confirmed) {
            return;
        }
        // update the state of c
        if (c->content == text) {
            if (!c->need_confirm) {
                init_connection(c);
            } else if (c->command_token == NULL) {
                c->set_write_message(1, text, tmp_buf);
                c->need_confirm = false;
            } else if (c->fp != NULL) { // there is a file
                c->set_write_message(0, data, NULL);
            } else {                    // there is no file
                c->file_name_len = 0;
                c->set_write_message(0, text, NULL);
            }
        } else if (c->content == data) {
            fclose(c->fp);
            c->fp = NULL;
            c->file_name_len = 0;
            c->set_write_message(0, text, NULL);
        }
        return;
    }
    static void run_put(Connection *c){
        char tmp_buf[1] = {'\0'};
        // prepare buffer
        if (c->action == transmit && !c->is_confirmed) {
            c->set_write_message(1, text, tmp_buf);
        } else if (c->action == receive) {
            if (c->content == text) {
                if (c->command_token == NULL) {
                    c->is_confirmed = true; 
                }
            } else if (c->content == data) {
                output_data(c, c->fp, c->read_byte);
            }
        }

        if (!c->is_confirmed) {
            return;
        }
        // update the state of c
        if (c->action == receive) {
            if (c->content == text) {
                c->file_name_len = strlen(c->read_buffer) + 1;
                if (c->file_name_len == 1) {
                    c->action = receive;
                    get_next_file_name(c);
                    if (c->command_token == NULL) {
                        init_connection(c);
                    } else {
                        c->set_read_message(text);
                    }
                } else {
                    strcpy(c->file_name, c->read_buffer);
                    c->fp = fopen(c->file_name, "w");
                    c->buffer_len = c->output_len = c->output_ptr = 0;
                    c->set_read_message(data);
                }
            } else if (c->content == data) {
                fclose(c->fp);
                c->action = receive;
                get_next_file_name(c);
                if (c->command_token == NULL) {
                    init_connection(c);
                } else {
                    c->set_read_message(text);
                }
            }
        } else if (c->action == transmit) {
            if (c->write_len != 0) {
                c->action = receive;
                c->need_confirm = true;
                c->set_read_message(text);
            }
        }
        return;
    }
    static void run_play(Connection *c){
        if (c->file_name_len == 0) {
            get_next_file_name(c);
            if (c->command_token == NULL) {
                c->is_confirmed = true;
            } else {
                c->file_name_len = strlen(c->command_token)+1;
                strcpy(c->file_name, c->command_token);
                if ((access(c->file_name, F_OK) == 0 && c->file_name_len >= 5) &&
                    strncmp(c->file_name+c->file_name_len-5, ".mpg", 4) == 0){
                    c->cap.open(c->file_name);
                    c->width = c->cap.get(CV_CAP_PROP_FRAME_WIDTH);
                    c->height = c->cap.get(CV_CAP_PROP_FRAME_HEIGHT);
                    c->img = Mat::zeros(c->height, c->width, CV_8UC3);    
                    c->img_size = c->img.total() * c->img.elemSize();
                    if(!c->img.isContinuous()){
                        c->img = c->img.clone();
                    }
                } else {
                    if (access(c->file_name, F_OK) != 0) {
                        c->width = c->height = -1;
                    } else {
                        c->width = c->height = -2;
                    }
                }
                char tmp_buf[NAME_SIZE];
                sprintf(tmp_buf, "%d %d", c->width, c->height);
                c->set_write_message(strlen(tmp_buf)+1, text, tmp_buf);
            }
        }
        if (c->content == data) {
            if (c->write_len != 0) { 
                int read_byte = read(c->sockfd, c->read_buffer, 1); 
                if (read_byte > 0) {
                    c->set_write_message(c->name_len, data, c->name);
                    c->data_size = 0;
                    return;
                }
            } 
            if (c->transmit_byte == c->img_size && c->write_byte == c->write_len) {
                if (c->cap.read(c->img)) {
                    c->write_len = c->write_byte = c->transmit_byte = 0;
                } else {
                    sprintf(c->write_buffer, "%s", c->name);
                    c->write_len = c->name_len;
                    c->transmit_byte = c->write_byte = c->data_size = 0;
                }
            }
            if (c->write_byte == c->write_len && c->data_size != 0) {
                c->write_len = min(BUFF_SIZE, c->img_size - c->transmit_byte);
                c->write_byte = 0;
                memcpy(c->write_buffer, c->img.data+c->transmit_byte, c->write_len);
                c->transmit_byte += c->write_len;
            }
        }
        
        if (!c->is_confirmed) {
            return;
        }
        if (c->content == text) {
            if (!c->need_confirm) {
                init_connection(c);
            }
            else if (c->command_token == NULL) {
                char tmp = '\0';
                c->set_write_message(1, text, &tmp);
                c->need_confirm = false;
            } else {
                if (c->height >= 0) {
                    c->transmit_byte = 0;
                    c->set_write_message(0, data, NULL);
                } else {
                    c->file_name_len = 0;
                    c->need_confirm = true;
                    c->set_write_message(0, text, NULL);
                }
            }
        } else if (c->content == data) {
            c->cap.release();
            c->file_name_len = 0;
            c->need_confirm = true;
            c->set_write_message(0, text, NULL);
        }
    }
    static int set_routine(Connection *c){
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
            printf("The command is wrong\n");
            return -1;
        }
        return 0;
    }
    static void setup_mission(Connection *c){
        c->action = transmit;
        if (c->routine == &run_ls) {
            c->dp = opendir(".");
            c->set_write_message(0, data, NULL);
        } else if (c->routine == &run_get) {
            c->fp = NULL;
            c->file_name_len = 0;
            c->need_confirm = true;
            c->set_write_message(0, text, NULL);
        } else if (c->routine == &run_put) {
            c->fp = NULL;
            c->file_name_len = 0;
            c->need_confirm = false;
            get_next_file_name(c);
            c->set_write_message(0, text, NULL);
        } else if (c->routine == &run_play) {
            c->cap.release();
            c->file_name_len = 0;
            c->need_confirm = true;
            c->set_write_message(0, text, NULL);
        }
    }
};
int main(int argc, char *argv[]){
    int port;
    if (argc != 2) {
        printf("usage: server ${port}\n");
        return 0;
    }
    srand(time(NULL));
    port = atoi(argv[1]);

    Server server(port);
    server.run();
    return 0;
}
