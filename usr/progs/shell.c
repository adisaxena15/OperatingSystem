
#include "syscall.h"
#include "string.h"
#include "shell.h"
#define BUFSIZE 1024
#define MAXARGS 8
//we need to create a helper to skip spaces
char* skip_spaces(char* p) {
	while (*p == ' ') p++;
	return p;
}
char* find_terminator(char* buf) {
	char* p = buf;
	while(*p) {
		switch(*p) {
			case ' ':
			case '\0':
			case FIN:
			case FOUT:
			case PIPE:
				return p;
			default:
				p++;
				break;
		}
	}
	return p;
}
//we need to create a helper to prepend "c/" to the command if needed
void make_full_path(char* cmd, char* fullpath, int maxlen) {
	//we need to check if the command already contains '/'
	char* slash = cmd;
	while (*slash && *slash != '/') slash++;
	if (*slash == '/') {
		//we need to already have a full path
		snprintf(fullpath, maxlen, "%s", cmd);
	} else {
		//we need to prepend "c/" to the command
		snprintf(fullpath, maxlen, "c/%s", cmd);
	}
}
int parse(char* buf, char** argv, char** pipe_pos) {
	int argc = 0;
	char* p = buf;
	*pipe_pos = NULL;
	//we need to skip leading spaces
	p = skip_spaces(p);
	while (*p && *p != PIPE) {
		//we need to start of a new argument
		char* arg_start = p;
		//we need to find the end of this token
		while (*p && *p != ' ' && *p != FIN && *p != FOUT && *p != PIPE) {
			p++;
		}
		char terminator = *p;
		*p = '\0';  
		//we need to handle special characters
		if (terminator == FIN) {
			//we need to handle input redirection: < filename
			p++;
			p = skip_spaces(p);
			char* filename = p;
			//we need to find the end of the filename
			while (*p && *p != ' ' && *p != PIPE && *p != FIN && *p != FOUT) p++;
			terminator = *p;
			*p = '\0';
			//we need to close stdin and open the file	
			_close(STDIN);
			int result = _open(STDIN, filename);
			if (result < 0) {
				printf("Error: cannot open input file '%s'\n", filename);
				return -1;
			}
		} 
		else if (terminator == FOUT) {
			// we need to handle output redirection: > filename
			p++;
			p = skip_spaces(p);
			char* filename = p;
			// we need to find the end of the filename
			while (*p && *p != ' ' && *p != PIPE && *p != FIN && *p != FOUT) {
				p++;
			}
			terminator = *p;
			*p = '\0';
			if (filename[0] == '\0') {
				printf("Error: invalid output redirection\n");
				return -1;
			}
			// close current STDOUT
			_close(STDOUT);
			_fsdelete(filename);         
			int result = _fscreate(filename);
			if (result < 0) {
				printf("Error: cannot create output file '%s'\n", filename);
				return -1;
			}
			result = _open(STDOUT, filename);
			if (result < 0) {
				printf("Error: cannot open output file '%s'\n", filename);
				return -1;
			}
		} 
		else {
			//we need to handle a regular argument
			if (*arg_start) { 
				argv[argc++] = arg_start;
				if (argc >= MAXARGS - 1) break;
			}
		}
		if (terminator == '\0' || terminator == PIPE) {
			if (terminator == PIPE) {
				*pipe_pos = p + 1; 
			}
			break;
		}
		p++;
		p = skip_spaces(p);
	}
	//we need to check if we hit a pipe
	if (*p == PIPE) {
		*pipe_pos = p + 1;
	}
	argv[argc] = NULL;
	return argc;
}

int main()
{
    char buf[BUFSIZE];
	char cmd_copy[BUFSIZE];
	char fullpath[BUFSIZE];
	int argc;
	char* argv[MAXARGS]; 
	char* pipe_pos;
	_open(CONSOLEOUT, "dev/uart1");		
	_close(STDIN);              		
	_close(STDOUT);              	
	_uiodup(CONSOLEOUT, STDOUT);     	
	printf("Starting 391 Shell\n");
	for (;;)
	{
		printf("LUMON OS> ");
		getsn(buf, BUFSIZE - 1);
		//we need to check for empty input
		char* check = buf;
		while (*check == ' ') check++;
		if (*check == '\0') continue;

		if (0 == strcmp(buf, "exit")) {
			_exit();
		}
		//we need to make a copy of the command for pipe handling
		snprintf(cmd_copy, BUFSIZE, "%s", buf);
		//we need to check if there's a pipe
		pipe_pos = NULL;
		argc = parse(cmd_copy, argv, &pipe_pos);
		if (argc < 0) {
			//we need to handle an error in parsing (e.g., file not found for redirection)
			//we need to reset the descriptors and continue
			_close(STDIN);
			_uiodup(CONSOLEOUT, STDIN);
			_close(STDOUT);
			_uiodup(CONSOLEOUT, STDOUT);
			continue;
		}
		if (argc == 0) {
			continue;
		}
		if (pipe_pos != NULL) {
			int wfd = -1, rfd = -1;
			if (_pipe(&wfd, &rfd) < 0) {
				printf("Error: pipe creation failed\n");
				_close(STDIN);
				_uiodup(CONSOLEOUT, STDIN);
				_close(STDOUT);
				_uiodup(CONSOLEOUT, STDOUT);
				continue;
			}
			//we need to fork the first child for the left side of the pipe
			int child1 = _fork();
			if (child1 == 0) {
				//we need to run the left command, writes to pipe
				_close(rfd);             
				_close(STDOUT);           
				_uiodup(wfd, STDOUT);     
				_close(wfd);              
				//we need to prepare and exec the left command
				make_full_path(argv[0], fullpath, BUFSIZE);
				int progfd = _open(-1, fullpath);
				if (progfd < 0) {
					printf("Error: cannot open program '%s'\n", fullpath);
					_exit();
				}
				_exec(progfd, argc, argv);
				printf("Error: exec failed for '%s'\n", argv[0]);
				_exit();
			}
			//we need to fork the second child for the right side of the pipe
			int child2 = _fork();
			if (child2 == 0) {
				//we need to run the right command, reads from pipe
				_close(wfd);              
				_close(STDIN);            
				_uiodup(rfd, STDIN);      
				_close(rfd);              
				//we need to parse the right command
				char* argv2[MAXARGS];
				char* dummy_pipe;
				int argc2 = parse(pipe_pos, argv2, &dummy_pipe);
				if (argc2 <= 0) {
					printf("Error: invalid right side of pipe\n");
					_exit();
				}
				//we need to prepare and exec the right command
				make_full_path(argv2[0], fullpath, BUFSIZE);
				int progfd = _open(-1, fullpath);
				if (progfd < 0) {
					printf("Error: cannot open program '%s'\n", fullpath);
					_exit();
				}
				_exec(progfd, argc2, argv2);
				printf("Error: exec failed for '%s'\n", argv2[0]);
				_exit();
			}
			//we need to close both pipe ends and wait for both children
			_close(wfd);
			_close(rfd);
			_wait(child1);
			_wait(child2);

		} else {
			//we need to handle a simple command without a pipe
			int child = _fork();
			if (child == 0) {
				//we need to run the command
				make_full_path(argv[0], fullpath, BUFSIZE);
				//we need to open the program file
				int progfd = _open(-1, fullpath);
				if (progfd < 0) {
					printf("Error: cannot open program '%s', rc=%d\n", fullpath, progfd);
					_exit();
				}
				//we need to execute the command
				_exec(progfd, argc, argv);
				//we need to handle if the exec returns an error
				printf("Error: exec failed for '%s'\n", argv[0]);
		_exit();
			} else {
				//we need to wait for the child
				_wait(child);
			}
		}
		//we need to reset the descriptors for the next command
		_close(STDIN);
		_uiodup(CONSOLEOUT, STDIN);
		_close(STDOUT);
		_uiodup(CONSOLEOUT, STDOUT);
	}
}
