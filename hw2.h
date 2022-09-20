#include <asm-generic/errno.h>
#include <arpa/inet.h>
#include <bits/types/struct_timespec.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h> 

#include <set>
#include <queue>
#include <algorithm>

#include <opencv2/opencv.hpp>

#define ERR_EXIT(a) { perror(a); exit(1); }
#define BUFF_SIZE 65536
#define NAME_SIZE 64
using namespace cv;
using std::set;
using std::queue;
enum Content{video, data, text};
enum Action{transmit, receive, input};
enum Command{_none, _ls, _put, _get, _play};
enum Identity{server, client};
class Connection {
public:
    int buffer_len,
        command_len,
        confirm_len,
        data_size,
        file_name_len,
        height,
        img_size,
        name_len,
        output_ptr,
        output_len,
        read_byte,
        read_len,
        sockfd,
        transmit_byte,
        write_len,
        write_byte,
        width;
    char buffer[2 * BUFF_SIZE],
         command_buffer[2 * BUFF_SIZE],
         confirm_buffer[NAME_SIZE],
         directory[BUFF_SIZE],
         file_name[BUFF_SIZE],
         name[NAME_SIZE],
         output_buffer[2 * BUFF_SIZE],
         read_buffer[2 * BUFF_SIZE],
         write_buffer[2 * BUFF_SIZE];
    char *command_token,
         *command_save_ptr;
    bool is_confirmed,
         need_confirm;
    FILE* fp;
    DIR* dp;
    struct dirent *dir;
    VideoCapture cap;
    Mat img;
    void (*routine)(Connection *);
    fd_set *write_mask,
           *read_mask;

    Action action;
    Content content;
    Command command;
    Identity identity;

    Connection(): fp(NULL), dp(NULL), dir(NULL), write_mask(NULL), read_mask(NULL) {}
    Connection(int sockfd): sockfd(sockfd), fp(NULL), dp(NULL), dir(NULL),
                            write_mask(NULL), read_mask(NULL) {}
    ~Connection() {
        close(sockfd);
        if (fp != NULL) {
            fclose(fp);
        }
        if (dp != NULL) {
            closedir(dp);
        }
        cap.release();
        return;
    }
    int send_and_confirm(bool readable = true, bool writable = true, bool need_confirm = false) {
        int tmp = 0;
        if (writable && write_len != 0) {
            tmp = send(sockfd, write_buffer+write_byte, write_len-write_byte, MSG_NOSIGNAL);
            if (tmp <= 0) {
                return -1;
            }
            write_byte += tmp;
            if ((content == text && write_byte >= write_len) ||
                (content == data && data_size == 0)) {
                if (need_confirm) {
                    set_read_message(content);
                } else {
                    is_confirmed = true;
                }
                return tmp;
            }
        }
        
        // confirm stage
        if (readable && write_len == 0 && need_confirm) {
            tmp = read(sockfd, read_buffer, BUFF_SIZE);
            if (tmp > 0) {
                is_confirmed = true;
                return 0;
            } else {
                return -1;
            }
        }
        return tmp;
    }
    int read_and_confirm(bool readable = true, bool writable = true, bool need_confirm = false) {
        int tmp = 0, start;
        char tmp_buf[NAME_SIZE];
        read_byte = 0;
        if (readable && write_len == 0) {
            tmp = read(sockfd, read_buffer, BUFF_SIZE);
            if (tmp <= 0) {
                return -1;
            }
            read_byte = tmp;
            if (content == data) {
                // need to trace the last NAME_SIZE chars
                if (read_byte >= NAME_SIZE) {
                    memcpy(confirm_buffer, read_buffer + read_byte-NAME_SIZE, NAME_SIZE);
                    confirm_len = NAME_SIZE;
                } else if (read_byte + confirm_len <= NAME_SIZE) {
                    memcpy(confirm_buffer+confirm_len, read_buffer, read_byte);
                    confirm_len += read_byte;
                } else {
                    start = read_byte + confirm_len - NAME_SIZE;
                    memcpy(tmp_buf, confirm_buffer + start , confirm_len - start);
                    memcpy(tmp_buf + confirm_len - start, read_buffer, read_byte);
                    memcpy(confirm_buffer, tmp_buf, NAME_SIZE);
                    confirm_len = NAME_SIZE;
                }
                start = 0;
                if (confirm_len >= name_len) {
                    start = confirm_len-name_len;
                }
            }
            if ((content == text && read_buffer[read_byte-1] == '\0') ||
                (content == data && strncmp(confirm_buffer+start, name, name_len) == 0)) {
                if (need_confirm) {
                    tmp_buf[0] = '\0';
                    set_write_message(1, content, tmp_buf);
                } else {
                    is_confirmed = true;
                }
                read_byte = tmp;
                return tmp;
            }
        }

        // confirm stage
        if (writable && write_len != 0) {
            if (send(sockfd, write_buffer+write_byte, write_len-write_byte, MSG_NOSIGNAL) > 0) {
                is_confirmed = true;
                return 0;
            } else {
                return -1;
            }
        }
        return tmp;
    }
    inline void set_write_message(int write_len, Content content, char *message) {
        this->write_len = write_len;
        this->read_byte = this->write_byte = 0;
        this->content = content;
        if (content == data) {
            this->data_size = -1;
        }
        this->is_confirmed = false;
        if (message != NULL) {
            memcpy(this->write_buffer, message, write_len);
        }
        if (read_mask != NULL) {
            FD_CLR(sockfd, read_mask);
        }
        if (write_mask != NULL) {
            FD_SET(sockfd, write_mask);
        }
    }
    inline void set_read_message(Content content) {
        this->content = content;
        this->is_confirmed = false;
        this->confirm_len = this->write_len = this->write_byte = this->read_byte = 0;
        if (read_mask != NULL) {
            FD_SET(sockfd, read_mask);
        }
        if (write_mask != NULL) {
            FD_CLR(sockfd, write_mask);
        }
    }
};
inline void init_command_token(Connection *c) {
    c->command_token = strtok_r(c->command_buffer, " ", &c->command_save_ptr);
}
inline void get_next_file_name(Connection *c) {
    c->command_token = strtok_r(NULL, " ", &c->command_save_ptr);
}
inline void output_data(Connection *c, FILE *fp, int read_byte) {
    for (int i = 0; i < read_byte; i++) {
        c->buffer[(c->buffer_len++)%(2*BUFF_SIZE)] = c->read_buffer[i];
    }
    c->output_len = 0;
    while (c->output_ptr + c->name_len < c->buffer_len) {
        c->output_buffer[c->output_len++] = c->buffer[(c->output_ptr++)%(2*BUFF_SIZE)];
    }
    c->output_ptr %= 2*BUFF_SIZE;
    fwrite(c->output_buffer, sizeof(char), c->output_len, fp);
}
