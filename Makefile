# To compile with test1, make test1
# To compile with test2, make test2
CC = clang -g -Wall
EXECUTABLE=sfs

SOURCES_TEST1= disk_emu.c sfs_api.c sfs_test1.c tests.c
SOURCES_TEST2= disk_emu.c sfs_api.c sfs_test2.c tests.c
LIBRARIES = -lm

test1: $(SOURCES_TEST1) 
	$(CC) -o $(EXECUTABLE) $(SOURCES_TEST1) $(LIBRARIES)

test2: $(SOURCES_TEST2)
	$(CC) -o $(EXECUTABLE) $(SOURCES_TEST2) $(LIBRARIES)
clean:
	rm $(EXECUTABLE)
