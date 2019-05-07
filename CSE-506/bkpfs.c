#include <asm/unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdlib.h>

typedef struct {
    int min_ver, max_ver;
    char filename[256];
} query_arg_t;


#define LIST_VERSIONS    	_IOWR('q', 1, query_arg_t *)
#define DELETE_VERSION   	_IOW('q', 2, int)
#define VIEW_VERSION   		_IOWR('q', 3, int)
#define RESTORE_VERSION  	_IOW('q', 4, int)

#define OLDEST_VERSION 		-2
#define NEWEST_VERSION 		-1
#define ALL_VERSIONS 		 0

int read_file(char *filename) {
	FILE *fptr;
	char c;

	fptr = fopen(filename, "r"); 
   	if (fptr == NULL) 
    	{ 
        	printf("Cannot open file \n"); 
        	exit(0); 
    	}
	c = fgetc(fptr); 
    	while (c != EOF) 
    	{ 
        	printf ("%c", c); 
        	c = fgetc(fptr); 
    	} 
    	fclose(fptr); 
    	return 0; 	 	
}

void list_versions(int fd) {
	query_arg_t q;
	int i = 0;        
	ioctl(fd, LIST_VERSIONS, &q);
        printf("Following are the backups for %s  : \n", q.filename);
	printf("------------------------------------------------------\n");
	for(i = q.min_ver; i < q.max_ver ; i++) 
		printf(".%s.%d.swp\n", q.filename, i);
}        	

void delete_version(int fd, int ver) {
	ioctl(fd, DELETE_VERSION, ver);
	printf("Done deleting the backup version\n");
}

void view_version(int fd, int ver, char* filename) {
	char *new_file, *s_version;
	int len = strlen(filename);
	int ver_len;   	
 
	ioctl(fd, VIEW_VERSION, ver);
    	new_file = (char *) malloc (len + 7);
    	strcpy(new_file, filename);
	
	ver_len = snprintf(NULL, 0, "%d", ver);
        s_version = (char *) malloc (ver_len + 1);
        snprintf(s_version, ver_len + 1, "%d", ver);

	strcat(new_file, ".");
	strcat(new_file, s_version);
    	strcat(new_file, ".vue");
	
	read_file(new_file);
	remove(new_file);
}

void restore_version(int fd, int ver) {
	ioctl(fd, RESTORE_VERSION, ver);
	printf("Restored the backup file\n");
}

void print_help() {
	printf("./bkpctl -[ld:v:r:] FILE\n");
	printf("FILE: the file's name to operate on\n");
	printf("-l: option to list versions\n");
	printf("-d ARG: option to 'delete' versions; ARG can be 'newest', 'oldest', or 'all'\n");
	printf("-v ARG: option to 'view' contents of versions (ARG: 'newest', 'oldest', or N)\n");
	printf("-r ARG: option to 'restore' file (ARG: 'newest' or N)\n");				
}

int main(int argc, char * const argv[]) {
	int err=0;
	int option = 0;
    	int fd = 0;
	int version;
	char *ver_str;	
    	char *optstring = "ld:v:r:h";
	char* file;

    	if ((option = getopt(argc, argv, optstring)) != -1) {
		switch(option) {
			printf("option: %c\n", option);
			case 'l':
				if (argc != 3) {
                                        print_help();
                                        return -1;
                                }
				break;
			case 'd':
			case 'v':
			case 'r':
				if (argc != 4) {
                        		print_help();
					return -1;
                		}
				ver_str = optarg;
				break;
			case 'h':
				print_help();
				goto out;
                        case '?':
                                printf("ERROR:Unknown option : %c \n", option);
				err = -EINVAL;
				goto out;
		}
    	}

        if(option != 'l' &&  optind + 1 != argc) {
                printf("INVOPT:Invalid file info \n");
                err = -EINVAL;
                goto out;
        } else {
		file = argv[optind];
	}
        
	fd = open(file, O_RDONLY);
        if(fd < 0) {
        	printf("Could Not Open Descriptor\n");
                return 1;
        }

	if(strcmp(ver_str, "newest") == 0)
		version = NEWEST_VERSION;
	else if(strcmp(ver_str, "oldest") == 0)
		version = OLDEST_VERSION;
	else if(strcmp(ver_str, "all") == 0)
		version = ALL_VERSIONS;
	else
		version = atoi(ver_str);

	switch(option) {
		case 'l':
			printf("INFO: List all files\n");
			list_versions(fd);
			break;
		case 'd':
			if(version > 0) {
				printf("Invalid version %d. Use [olderst | newest | all]\n", version);
				err = -EINVAL;
				goto out;
			}
			delete_version(fd, version);
			break;
                case 'v':
			view_version(fd, version, file);
			break;
                case 'r':
			restore_version(fd, version);
			break;

	}    
out:
	close(fd); 
	return err;
}
