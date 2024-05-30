// *******************************************************
// Version ..... : V4.0 du 30/05/2024
// 
// Fonctionnalités :
// 		- les pipes
//   	- les processus en arrière-plan
// 		- les commandes internes
//		- les redirections
//
// ********************************************************/

# include "sys.h"
#include <string.h>
#include <ctype.h>	// pour isspace()

int decouper(char *, char *, char *[], int);
int est_vide(char *);

int lance_cmd(char *);
int exec_pipeline(char *);
int traite_pipe(char * []);

int arriere_plan(char *);

int check_interne(char *);
int cmd_internes(char * []);
int moncd(char *[]);

int redirige(char *, char *);
int check_redir(char **);

/* Convertit un caractère [0-9] en l'entier correspondant */
int chiffre(char c) {return c - '0';}

enum {
	MaxLigne = 1024, 			// longueur max d'une ligne de commandes
	MaxMot = MaxLigne / 2, 		// nbre max de mot dans la ligne
	MaxDirs = 100, 				// nbre max de repertoire dans PATH
	MaxPathLength = 512, 		// longueur max d'un nom de fichier
	MaxPipes = 128,				// nbre max de commandes séparées par " | " sur une ligne
};

# define PROMPT "V4.0 "
char * path[MaxDirs];			// liste des répertoires de PATH (globale ???)

int main(int argc, char * argv[]){
	
	char ligne[MaxLigne];			// la dernière ligne entrée par l'utilisateur
	int tmp, status, retour;

	/* Découper UNE COPIE de PATH en repertoires */
	decouper(strdup(getenv("PATH")), ":", path, MaxDirs);

	/* Lire et traiter chaque ligne saisie dans le terminal */
	for (printf(PROMPT); fgets(ligne, sizeof ligne, stdin) != 0; printf(PROMPT)){

		if (!est_vide(ligne)){
			int bg = arriere_plan(ligne);

			/* si premier plan, vérifier si c'est une commande interne avant de fork */
			if (!bg && (check_interne(ligne)==0))
					continue;

			/* lancement du processus fils qui va exécuter la pipeline */
			if ((tmp = fork()) < 0){
				perror("fork");
				continue;}

			/* enfant : prend en charge  la pipeline */
			if (tmp == 0)
				exec_pipeline(ligne);	// pour l'instant, une seule pipeline

			/* parent : attendre la fin de l'enfant (ou pas) */
			if (bg) printf("bg : %d\n", tmp);		// bg : on affiche le PID du fils
			else {
				if (waitpid(tmp, &status, 0) < 0)	// fg : on attend la fin du fils
					perror("wait");}
		}
		/* Récupérer les éventuels processus lancés en arrière-plan qui sont terminés */
		while ((retour = waitpid(-1, &status, WNOHANG)) > 0)
			if (retour > 0 ) printf("[Processus %i terminé]\n", retour);
	}
	printf("Bye\n");
	return 0;
}

/* Exécute une pipeline (une série de commandes séparées par des "|" ) */
int exec_pipeline(char * pipeline){
	char * liste_cmd[MaxPipes];		// la ou les (en cas de pipe(s)) commande(s) de la ligne
	int i = 0;

	/* Séparer les différentes commandes en cas de pipe */
	decouper(pipeline, "|", liste_cmd, MaxPipes);

	/* plusieurs commandes successives */
	if (liste_cmd[1] != 0) {
		i = traite_pipe(liste_cmd);
		if (i < 0) {
			fprintf(stderr, "Echec de la commande [%d] : \"%s\".\n", -i, liste_cmd[-i]);
			exit(-1); }
	}

	/* dernière (ou unique) commande de la pipeline */
	lance_cmd(liste_cmd[i]);
	return 0;
}

int traite_pipe(char * commandes[]){
	int pipe_fd[2];				
	int cmd, i;
	int in = 0;

	for (i = 0; commandes[i+1] != 0; i++) {	
		if (est_vide(commandes[i])){
			fprintf(stderr, "Erreur de syntaxe. ");		// fprintf ou pas nécessaire ?
			return -i;}

		/* ouvrir un pipe pour communiquer avec le processus suivant */	
		if (pipe(pipe_fd) != 0) {
			perror("pipe");
			return -i;}

		/* créer un enfant pour exécuter la "sous-commande" */
		if ((cmd = fork()) < 0){
			perror("fork");
			return -i;}

		if (cmd == 0){				// enfant
			close(pipe_fd[0]); 		// ferme la sortie du tube (inutilisée)
			dup2(pipe_fd[1], 1); 	// redirige sa sortie sur l'entrée du tube
			close(pipe_fd[1]);
			dup2(in, 0);			// redirige la sortie utilisée précédemment sur son entrée
			close(in);
			lance_cmd(commandes[i]);	// exécute la commande
		}
		/* Parent */
		close(pipe_fd[1]);			// fermeture de l'entrée du tube actuel
		if (in != 0) close(in);		
		in = pipe_fd[0];
	}

	dup2(in, 0);		// récupère en entrée la sortie du dernier pipe
	close(in); 
	return i;
}

/* exécute une commande, en cherchant l'exécutable dans la liste de répertoires fournie */
int lance_cmd(char * commande){	
	char * mot[MaxMot];				// liste des mots de la commande à exécuter
	char pathname[MaxPathLength];

	if (decouper(commande, " \t\n", mot, MaxMot) == 0){
		fprintf(stderr, "Erreur de syntaxe (manque la dernière commande)\n");
		exit(-1);}

	if (cmd_internes(mot) == 0) exit(0);

	check_redir(mot);					// éventuelles redirections d'E/S

	for (int i = 0; path[i] != 0; i ++){
		snprintf(pathname, sizeof pathname, "%s/%s", path[i], mot[0]);
		execv(pathname, mot);
	}
	fprintf(stderr, "%s: not found\n", mot[0]);
	exit(-1);
}

/* découpe une chaîne de caractères et renvoie le nombre de mots récupérés */
int decouper(char * ligne, char * separ, char * mot[], int maxmot){	
	int i ;
	mot[0] = strtok(ligne, separ);
	for(i = 1; mot[i-1] != 0; i ++){
		if (i == maxmot){
			fprintf (stderr, "Erreur dans la fonction decouper: trop de mots\n");
			mot[i-1] = 0;
			break;
		}
		mot[i] = strtok(NULL, separ);
	}
	return (i-1);
}

/* Renvoie 1 si la chaîne de caractères ne contient que des espaces, 0 sinon */
int est_vide(char *str) {
    while (*str) {
        if (!isspace(*str))
            return 0;
        str++;
    }
    return 1;
}

/* Renvoie */
int arriere_plan(char * ligne){
	strtok(ligne, "&");
	return(strtok(NULL, "") != NULL);
}

/* change de répertoire courant */
int moncd(char * commande[]){
	char * dir;
	int len;

	for (len = 0; commande[len] != 0; len ++);	// nombre de mots

	if (len < 2){
		dir = getenv("HOME");
		if (dir == 0) dir = "/tmp";}
	else if (len > 2){
		fprintf(stderr, "usage: %s [dir]\n", commande[0]);
		return 1;}
	else 
		dir = commande[1];
	
	if (chdir(dir) < 0){
		perror(dir);
		return 1;}
	return 0;
}

/* Renvoie 0 si une commande interne a été trouvée et exécutée */
int check_interne(char * commande){
	/* s'il y a des pipes, ne pas exécuter la commande depuis le shell (on va fork d'abord) */
	if (strchr(commande, '|') != NULL)
		return 1;
	char * mot[MaxMot];				// liste des mots de la commande à exécuter
	decouper(strdup(commande), " \t\n", mot, MaxMot);
	return cmd_internes(mot);
}

/* si l'entrée correspond à  une commande interne, l'exécuter et renvoyer 0, sinon 1 */
int cmd_internes(char * commande[]){	
	if (strcmp("moncd", commande[0]) == 0){
		moncd(commande);
		return 0;
	}
	// commande de sortie du shell
	else if (strcmp("monexit", commande[0]) == 0)
		exit(0);
	return 1;
}

/* Réalise une redirection basique (<, >, >>, 2>, 2>>, 2>&1, 1>&2) */
int redirige(char * op, char * fichier)
{	int stream;		// descripteur du fichier du / vers lequel on redirige
	int fd;			// flux standard concerné par la redirection (0, 1 ou 2)

	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;  // permissions pour la création de fichier, ici = 644

	if (strcmp(op, "<") == 0) {				// mode lecture
		stream = open(fichier, O_RDONLY);
		fd = 0; }
	else if (strcmp(op, ">") == 0) {		// mode écriture avec création si nécessaire
		stream = open(fichier, O_WRONLY | O_CREAT | O_TRUNC, mode);
		fd = 1; }
	else if (strcmp(op, ">>") == 0){
		stream = open(fichier, O_WRONLY | O_CREAT | O_APPEND, mode);
		fd = 1; }
	else if (strcmp(op, "2>") == 0){
		stream = open(fichier, O_WRONLY | O_CREAT | O_TRUNC, mode);
		fd = 2; }
	else if (strcmp(op, "2>>") == 0){
		stream = open(fichier, O_WRONLY | O_CREAT | O_APPEND, mode);
		fd = 2; }
	else if (op[1] == '>' && op[2] == '&'){		// opérateur de type 2>&1
		fd = chiffre(op[0]);					// flux à rediriger (premier entier de l'opérateur)
		stream = chiffre(op[3]);}				// flux vers lequel rediriger (second entier)
	else {							// si rien de tout ça
		fprintf(stderr, "Erreur : redirection \"%s\" non reconnue.\n", op);
		return -1; }
	
	if (stream < 0) {				// erreur dans l'ouverture du fichier
			perror("open");
			return -1; }
	
	dup2(stream, fd);				// redirection
	if (stream > 2) close(stream);	// fermeture du fichier (hors cas de fusion de flux standards type 2>&1)
	return 0;
}

/* Parcourt un vecteur de mots pour en extraire les redirections */
int check_redir(char * mots[]){
	int i = 0;			// index pour recopier les mots non liés à la redirection
	char op[5];			// opérateur (opérande ?)
	
	for(int k = 0; mots[k] ; k++)
	{	
		/* si c'est un opérateur de redirection (4 caractères max, parmi 1, 2, >, < et &) */
		if (sscanf(mots[k], "%4[12><&]", op))
		{	
			/* opérateur à trois symboles max (redirection vers/depuis un fichier) */
			if (strlen(mots[k]) < 4) {		
				if (mots[k+1] == NULL) { 		// dernier mot de la ligne
					fprintf(stderr, "Echec de redirection : manque le nom du fichier.\n");
					continue;}			
				redirige(mots[k], mots[k+1]);	// faire la redirection
				k++;							// ignorer le mot suivant (nom du fichier)
				}
			/* opérateur type 2>&1 (redirections entre flux standards) */
			else redirige(mots[k], NULL);		// faire la redirection
		}	
		else mots[i++] = mots[k];				// ne recopier que les autres mots
	}
	mots[i]=NULL;								// mettre à jour la fin du vecteur
	return 0;	}
