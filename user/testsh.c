#include <inc/lib.h>

char *TEST_FILE_NAME = "test";

struct SnapForTest {
    char *name;
    char *comment;
    char *data;
    size_t data_size;
};

struct SnapForTest snap1 = {
    .name = "first_snap",
    .comment = "comment1",
    .data = "1",
    .data_size = 2};

struct SnapForTest snap2 = {
    .name = "second_snap",
    .comment = "comment2",
    .data = "2",
    .data_size = 2};

struct SnapForTest snap3 = {
    .name = "third_snap",
    .comment = "comment3",
    .data = "3",
    .data_size = 2};

struct SnapForTest snap4 = {
    .name = "fourth_snap",
    .comment = "comment4",
    .data = "4",
    .data_size = 2};

struct SnapForTest snap5 = {
    .name = "fifth_snap",
    .comment = "comment5",
    .data = "5",
    .data_size = 2};

struct SnapForTest snap6 = {
    .name = "six_snap",
    .comment = "comment6",
    .data = "6",
    .data_size = 2};

void
umain(int argc, char **argv) {

    char buf[3];
    int fd;
    
    fd = open(TEST_FILE_NAME, O_CREAT | O_TRUNC | O_WRONLY);
    write(fd, snap1.data, snap1.data_size);
    close(fd);
    create_snapshot(snap1.comment, snap1.name);

    fd = open(TEST_FILE_NAME, O_CREAT | O_TRUNC | O_WRONLY);
    write(fd, snap2.data, snap2.data_size);
    close(fd);
    create_snapshot(snap2.comment, snap2.name);

    fd = open(TEST_FILE_NAME, O_CREAT | O_TRUNC | O_WRONLY);
    write(fd, snap3.data, snap3.data_size);
    close(fd);
    create_snapshot(snap3.comment, snap3.name);

    fd = open(TEST_FILE_NAME, O_CREAT | O_TRUNC | O_WRONLY);
    write(fd, snap4.data, snap4.data_size);
    close(fd);
    create_snapshot(snap4.comment, snap4.name);

    // print_snapshot_list();

    // cprintf("\n\n\n**************************************************************************\n\n\n");

    accept_snapshot(snap3.name);

    // print_snapshot_list();

    fd = open(TEST_FILE_NAME, O_RDONLY);
    read(fd, buf, snap3.data_size);
    // cprintf("snap3 data: %s; but expected: %s\n", buf, snap3.data);
    assert(!strcmp(buf, snap3.data));
    close(fd);

    accept_snapshot(snap4.name);

    // print_snapshot_list();

    fd = open(TEST_FILE_NAME, O_RDONLY);
    read(fd, buf, snap4.data_size);
    assert(!strcmp(buf, snap4.data));
    close(fd);

    accept_snapshot(snap3.name);

    // print_snapshot_list();

    fd = open(TEST_FILE_NAME, O_RDONLY);
    read(fd, buf, snap3.data_size);
    //cprintf("snap3 data: %s; but expected: %s\n", buf, snap3.data);
    assert(!strcmp(buf, snap3.data));
    close(fd);

    fd = open(TEST_FILE_NAME, O_CREAT | O_TRUNC | O_WRONLY);
    write(fd, snap5.data, snap5.data_size);
    close(fd);
    create_snapshot(snap5.comment, snap5.name);

    // print_snapshot_list();

    accept_snapshot(snap3.name);

    // print_snapshot_list();

    fd = open(TEST_FILE_NAME, O_RDONLY);
    read(fd, buf, snap3.data_size);
    // cprintf("snap3 data: %s; but expected: %s\n", buf, snap3.data);
    assert(!strcmp(buf, snap3.data));
    close(fd);

    fd = open(TEST_FILE_NAME, O_CREAT | O_TRUNC | O_WRONLY);
    write(fd, snap6.data, snap6.data_size);
    close(fd);
    create_snapshot(snap6.comment, snap6.name);

    print_snapshot_list();

    return;
}