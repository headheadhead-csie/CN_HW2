#include "hw2.h"
#include <opencv2/highgui.hpp>

#define PORT 8787
#define ERR_EXIT(a) { perror(a); exit(1); }
class Client {
public:
    Client(int port = 8787, char* server_ip = NULL, int client_id = 1) {
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
    ~Client() {
        close(sockfd);
    }
    void run() {
        char tmp_buf[NAME_SIZE];
        sprintf(tmp_buf, "b08902062_%d_client_folder", id);
        mkdir(tmp_buf, 0755);
        chdir(tmp_buf);
        init_connection(c);

        // First, we have to send ID to the server
        // The server shouldn't crash, there is no need to handle
        // such cases.
        c->set_read_message(text);
        while(!c->is_confirmed) {
            c->read_and_confirm();
        }
        strcpy(c->name, c->read_buffer); 
        c->name_len = strlen(c->name) + 1;

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
    static void init_connection(Connection *c) {
        c->action = receive;
        c->is_confirmed = false;
        c->routine = run_none;
        c->need_confirm = false;
        c->output_len = c->output_ptr = c->buffer_len = c->file_name_len = 0;
    }
    static void run_none(Connection *c) {
        fgets(c->command_buffer, BUFF_SIZE, stdin);
        c->command_buffer[strlen(c->command_buffer)-1] = '\0';
        init_connection(c);
        return;
    }
    static void run_ls(Connection *c) {
        int read_byte;

        c->set_read_message(data);
        while (!c->is_confirmed) {
            read_byte = c->read_and_confirm();
            output_data(c, stdout, read_byte);
        }
        return;
    }
    static void run_get(Connection *c) {
        int read_byte;
        get_next_file_name(c);
        while (c->command_token) {
            read_byte = 0;
            c->set_read_message(text);

            while (!c->is_confirmed) {
                read_byte += c->read_and_confirm(true, true, true);
            }
            strcpy(c->file_name, c->read_buffer);
            c->file_name_len = read_byte;
            if (c->file_name_len == 1) {
                printf("The %s doesn't exist.\n", c->command_token);
                get_next_file_name(c);
                continue;
            }
            printf("getting file %s......\n", c->command_token);
            c->fp = fopen(c->file_name, "w");
            c->set_read_message(data);
            while (!c->is_confirmed) {
                read_byte = c->read_and_confirm(true, true, true);
                output_data(c, c->fp, read_byte);
            }
            fclose(c->fp);
            c->buffer_len = c->output_ptr = c->output_len = 0;
            get_next_file_name(c);
        }
        c->set_read_message(text);
        while (!c->is_confirmed) {
            c->read_and_confirm();
        }

        c->fp = NULL;
        return;
    }
    static void run_put(Connection *c) {
        char tmp_buf[1] = {'\0'};
        c->set_read_message(text);
        while (!c->is_confirmed) {
            c->read_and_confirm();
        }

        get_next_file_name(c);
        while (c->command_token) {
            strcpy(c->file_name, c->command_token);
            c->file_name_len = strlen(c->file_name) + 1;
            c->fp = fopen(c->file_name, "r");
            if (c->fp == NULL) {
                c->set_write_message(1, text, tmp_buf);
            } else {
                c->set_write_message(c->file_name_len, text, c->file_name);
            }
            while (!c->is_confirmed) {
                c->send_and_confirm(true, true, true);
            }
            if (c->fp == NULL) {
                printf("The %s doesn't exist.\n", c->command_token);
                get_next_file_name(c);
                continue;
            }
            printf("putting %s......\n", c->command_token);
            c->set_write_message(0, data, NULL);
            while (!feof(c->fp)) {
                c->write_byte = 0;
                c->write_len = fread(c->write_buffer, sizeof(char), BUFF_SIZE, c->fp);
                if (feof(c->fp)) {
                    sprintf(c->write_buffer+c->write_len, "%s", c->name);
                    c->write_len += c->name_len;
                    c->data_size = 0;
                    while (!c->is_confirmed) {
                        c->send_and_confirm(true, true, true);
                    }
                } else {
                    while (c->write_byte < c->write_len) {
                        c->send_and_confirm(true, true, true);
                    }
                }
            }
            fclose(c->fp);
            get_next_file_name(c);
        }
        return;
    }
    static void run_play(Connection *c) {
        int read_byte, img_size;
        char *save_ptr, *token, tmp;

        get_next_file_name(c);
        while (c->command_token) {
            img_size = read_byte = 0;
            c->set_read_message(text);
            while (!c->is_confirmed) {
                read_byte += c->read_and_confirm(true, true, true);
            }
            token = strtok_r(c->read_buffer, " ", &save_ptr);
            c->width = atoi(token);
            token = strtok_r(NULL, " ", &save_ptr);
            c->height = atoi(token);
            if (c->width < 0) {
                if (c->width == -1) {
                    printf("The %s doesn't exist.\n", c->command_token);
                } else {
                    printf("The %s is not a mpg file.\n", c->command_token);
                }
                get_next_file_name(c);
                continue;
            }
            printf("playing the video.\n");

            c->img = Mat::zeros(c->height, c->width, CV_8UC3);
            if(!c->img.isContinuous()) {
                 c->img = c->img.clone();
            }
            c->img_size = c->img.total() * c->img.elemSize();
            c->set_read_message(data);

            img_size = read_byte = 0;
            while (!c->is_confirmed) {
                read_byte = c->read_and_confirm(true, true, true);
                if (img_size + read_byte >= c->img_size) {
                    int transfer_size = 0, read_ptr = 0;
                    const int total_size = img_size + read_byte;
                    while (total_size - transfer_size >= c->img_size) {
                        memcpy(c->img.data+img_size, c->read_buffer+read_ptr,
                               c->img_size-img_size);
                        imshow(c->name, c->img);
                        read_ptr += c->img_size - img_size;
                        transfer_size += c->img_size;
                        img_size = 0;
                    }
                    memcpy(c->img.data, c->read_buffer+read_ptr, total_size-transfer_size);
                    img_size = total_size-transfer_size;
                    tmp = (char)waitKey(33);
                    if (tmp == 27) {
                        tmp = '\0';
                        c->set_write_message(1, text, &tmp);
                        c->send_and_confirm();
                        c->set_read_message(data);
                        while (!c->is_confirmed) {
                            c->read_and_confirm(true, true, true);
                        }
                        break;        
                    }
                } else {
                    memcpy(c->img.data+img_size, c->read_buffer, read_byte);
                    img_size += read_byte;
                }
            }
            c->cap.release();
            destroyAllWindows();
            get_next_file_name(c);
        }
        c->set_read_message(text);
        while (!c->is_confirmed) {
            c->read_and_confirm();
        }
        return;
    }
    int set_routine(Connection *c) {
        c->command_len = strlen(c->command_buffer)+1;
        c->set_write_message(c->command_len, text, c->command_buffer);
        init_command_token(c);
        if (c->command_token == NULL) {
            return -1; 
        } else if (strncmp(c->command_token, "ls", 2) == 0) {
            c->routine = &run_ls;
        } else if (strncmp(c->command_token, "put", 3) == 0) {
            c->routine = &run_put;
        } else if (strncmp(c->command_token, "get", 3) == 0) {
            c->routine = &run_get;
        } else if (strncmp(c->command_token, "play", 4) == 0) {
            c->routine = &run_play;
        } else {
            printf("Commad format error.");
            return -1;
        }
        c->send_and_confirm();
        return 0;
    }
};
int main(int argc , char *argv[]) {
    int client_id, port;
    char *server_ip;
    if (argc != 3) {
        printf("usage: server $client_id ${ip}:${port}\n");
        return 0;
    }

    client_id = atoi(argv[1]);
    server_ip = strtok(argv[2], ":");
    port = atoi(strtok(NULL, ":"));


    Client client(port, server_ip, client_id);
    client.run();

    return 0;
}

