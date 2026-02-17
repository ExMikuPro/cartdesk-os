#ifndef CART_READER_H
#define CART_READER_H

int cart_fs_init(void);
int cart_read_title_from_sd(const char* path, char out_title[65]);

#endif
