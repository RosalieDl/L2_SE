#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>			// pour isgraph() 
#include <readline/readline.h>

int execute(char *);
int lance_cmd(char *[]);
int exec_pipeline(char *);
bool cmd_internes(char * []);
int moncd(char *[]);
int check_redir(char **);
int redirige(char *, char *);
int decouper(char *, char *, char *[], int);
bool premier_plan(char *);
bool est_vide(char *);
int chiffre(char);
int setmanpath();