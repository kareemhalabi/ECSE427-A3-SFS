/* sfs_test.c 
 * 
 * Written by Xiru Zhu for Assignment 3. 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "sfs_api.h"

/* The maximum file name length. We assume that filenames can contain
 * upper-case letters and periods ('.') characters. Feel free to
 * change this if your implementation differs.
 */
#define MAX_FNAME_LENGTH 10   /* Assume at most 20 characters (16.3) */

/* The maximum number of files to attempt to open or create.  NOTE: we
 * do not _require_ that you support this many files. This is just to
 * test the behavior of your code.
 */
#define MAX_FD 32 
/* The maximum number of bytes we'll try to write to a file. If you
 * support much shorter or larger files for some reason, feel free to
 * reduce this value.
 */
#define MAX_BYTES 28672 // NOTE: CHANGED FROM DEFAULT OF 30000
//The maximal number of bytes I will write for a single write
#define MAX_WRITE_BYTE 2025


//Don't change these values
#define ABS_CAP_FD        4092
#define ABS_CAP_FILE_SIZE 2000000

static char test_str[] = "Graphic Card is life. I need a 1080 for Christmas. As a side note, it's quite obvious when people copy from github. Usually there's more than one guy copying for the same repository.\n";
 
//Random Text Generators
char *rand_name();

//Seek
int test_seek(int *file_id, int *file_size, int *write_ptr, char **write_buf, int num_file, int offset, int *err_no);

//Read Functions
int test_read_all_files(int *file_id, int *file_size, char **write_buf, int num_file, int *err_no);
int test_simple_read_files(int *file_id, int *file_size, char **write_buf, int num_file, int *err_no);
int test_difficult_read_files(int *file_id, int *file_size, int *write_ptr, char **write_buf, int index, int read_length, int *err_no);
int test_random_read_files(int *file_id, int *file_size, int *write_ptr, char **write_buf ,int num_file, int *err_no);

//Write Function
int test_simple_write_files(int *file_id, int *file_size, int *write_ptr, char **write_buf, int num_file, int *err_no);
int test_difficult_write_files(int *file_id, int *file_size, int *write_ptr, char **write_buf, int num_file, int *err_no);
int test_write_to_overflow(int *file_id, int *file_size, char **write_buf, int num_file, int *err_no);
int test_read_write_out_of_bound(int *file_id, int *file_sizes, char **file_names, int num_file, int *err_no);

//Close/Remove Functions
int test_remove_files(int *file_id, int *file_size, int *write_ptr, char **file_names, char **write_buf, int num_file, int *err_no);
int test_close_files(char **file_names, int *file_id, int num_file, int *err_no);

//Open file functions
int test_open_new_files(char **file_names, int *file_id, int num_file, int *err_no);
int test_open_old_files(char **file_names, int *file_id, int num_file, int *err_no);
int test_overflow_open(int *file_id, int *file_sizes, int *write_ptr, char **file_names, char **write_buf, int num_file, int *err_no);

//Test persistence
int test_persistence(int *error, int write_length);

//Help functionn
int free_name_element(char **name_list, int num_file);
